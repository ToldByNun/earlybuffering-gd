#include "AssistController.hpp"
#include "LevelProfile.hpp"
#include "ShadowSimulator.hpp"
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"
#include "../ui/StatusOverlay.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace frire {

namespace {
constexpr int JUMP_BUTTON = 1;

int roundBias(float bias) {
    if (bias >= 0.f) return static_cast<int>(std::lround(bias));
    return static_cast<int>(std::lround(bias));
}
}

AssistController& AssistController::get() {
    static AssistController instance;
    return instance;
}

bool AssistController::isInjecting() const { return m_injecting; }
BufferedInput const& AssistController::buffer() const { return m_buffer; }
int AssistController::attemptTick() const { return m_attemptTick; }
float AssistController::lastDt() const { return m_lastDt; }

double AssistController::estimatedTps() const {
    if (m_lastDt <= 0.f) return 240.0;
    return 1.0 / static_cast<double>(m_lastDt);
}

std::string AssistController::overlayText() const { return m_overlayText; }

void AssistController::clearBuffer() { m_buffer = {}; }

void AssistController::syncButtonState(PlayLayer* playLayer) {
    if (!playLayer) return;
    PlayerObject* players[2] = { playLayer->m_player1, playLayer->m_player2 };
    for (int i = 0; i < 2; ++i) {
        auto* p = players[i];
        if (!p) continue;
        auto it = p->m_holdingButtons.find(static_cast<int>(PlayerButton::Jump));
        bool const gdDown = it != p->m_holdingButtons.end() && it->second;
        if (gdDown && !m_userHolding[i]) {
            m_injecting = true;
            p->releaseButton(PlayerButton::Jump);
            m_injecting = false;
            AssistLog::debug(fmt::format("button sync: released stuck jump button (p{})", i + 1));
        }
    }
}

void AssistController::resetAttempt(PlayLayer* playLayer) {
    clearBuffer();
    m_pendingRelease = false;
    m_attemptTick = 0;
    m_inRealUpdate = false;
    m_subDtSeen = 0.f;
    m_modeKnown = false;
    m_log.clear();
    syncButtonState(playLayer);
    updateOverlay();
}

void AssistController::onLevelInit(PlayLayer* playLayer) {
    m_userHolding[0] = m_userHolding[1] = false;
    resetAttempt(playLayer);
    ShadowSimulator::get().attach(playLayer);
    StatusOverlay::attach(playLayer);
    if (playLayer && playLayer->m_level) {
        LevelProfile::get().loadForLevel(playLayer->m_level);
    }
    updateOverlay();
}

void AssistController::onLevelQuit() {
    LevelProfile::get().save();
    ShadowSimulator::get().detach();
    StatusOverlay::detach();
    LevelProfile::get().clear();
    resetAttempt();
}

void AssistController::resetLevelProfile() {
    LevelProfile::get().resetCurrent();
    updateOverlay();
}

void AssistController::beginRealUpdate() {
    m_inRealUpdate = true;
    m_subDtSeen = 0.f;
}

void AssistController::endRealUpdate() {
    m_inRealUpdate = false;
}

void AssistController::onSubStep(PlayerObject* player, float dt) {
    if (!m_inRealUpdate || dt <= 0.f) return;
    auto* pl = PlayLayer::get();
    if (!pl || player != pl->m_player1) return;
    m_subDtSeen += dt;
}

int AssistController::activeLimit(PlayerObject* player) const {
    return StageConfig::paramsFor(player).ticks;
}

bool AssistController::shouldAssistPlayer(PlayerObject* player) const {
    if (!StageConfig::isEnabled()) return false;
    if (!player || player->m_isDead) return false;
    return StageConfig::paramsFor(player).enabled;
}

void AssistController::updateOverlay() {
    if (!StageConfig::showOverlay()) {
        StatusOverlay::setVisible(false);
        return;
    }
    auto const stage = StageConfig::fromSettings();
    char const* state = "idle";
    if (m_buffer.state == BufferState::Buffered) state = m_buffer.down ? "buffer press" : "buffer release";
    else if (m_buffer.state == BufferState::FiredHolding) state = "hold";

    if (!stage.enabled) {
        m_overlayText = "frirecracker: OFF";
    }
    else {
        int left = 0;
        if (m_buffer.state == BufferState::Buffered) left = m_buffer.fireTick - m_attemptTick;

        if (StageConfig::autoTune()) {
            auto spot = LevelProfile::get().overlaySpot(m_lastMode, m_lastPlayerX);
            int const biasI = roundBias(spot.bias);
            m_overlayText = fmt::format(
                "frirecracker: {} | {} left={} | learn bias{:+d} trust {:.1f} n={}",
                StageConfig::stageLabel(stage),
                state,
                left,
                biasI,
                spot.trust,
                spot.n
            );
        }
        else {
            m_overlayText = fmt::format(
                "frirecracker: {} | {} left={} | learn OFF",
                StageConfig::stageLabel(stage),
                state,
                left
            );
        }
    }
    StatusOverlay::update(m_overlayText);
}

void AssistController::inject(GJBaseGameLayer* layer, bool down) {
    if (!layer) return;
    // Timeline already seeded in recordDecision with the planned fire tick.
    m_injecting = true;
    layer->handleButton(down, JUMP_BUTTON, m_buffer.player1);
    m_injecting = false;
}

void AssistController::noteRealJump(PlayerObject* player) {
    if (m_buffer.state != BufferState::FiredHolding) return;
    auto* pl = PlayLayer::get();
    if (!pl) return;
    auto* ours = m_buffer.player1 ? pl->m_player1 : pl->m_player2;
    if (player == ours) m_buffer.jumpSeen = true;
}

void AssistController::recordDecision(
    PlayLayer* playLayer, PlayerObject* player, bool down,
    SimPlan const& plan, int effectTick, int appliedDelay, int limit
) {
    if (!playLayer || !player) return;

    DecisionEntry e;
    e.tick = effectTick;
    e.x = player->getPositionX();
    e.mode = StageConfig::kindOf(player);
    e.down = down;
    e.delay = appliedDelay;
    e.limit = limit;
    e.firstDeathTick = plan.firstDeathTick;
    e.action = appliedDelay > 0 ? PlanAction::Delay : plan.action;
    e.nowIsSafe = plan.nowIsSafe;
    e.assisted = appliedDelay > 0;
    e.horizon = ShadowSimulator::modelFor(player, limit, LevelProfile::get().capMul(e.mode)).cap;
    e.snapshotSlot = ShadowSimulator::get().captureSnapshot(player);
    // Seed timeline with the planned action at fire tick.
    int const fire = effectTick + appliedDelay;
    e.timeline.push_back(InputEvent{ fire, down });
    m_log.push(std::move(e));
}

void AssistController::tickLearning(PlayLayer* playLayer, PlayerObject* player) {
    if (!StageConfig::autoTune() || !playLayer || !player) return;
    auto& profile = LevelProfile::get();
    if (!profile.loaded()) return;

    for (auto& e : m_log.entries()) {
        if (e.resolved) continue;
        int const age = m_attemptTick - e.tick;
        if (age < 0) continue;

        // Pessimism: Pass with predicted death, but player still alive past that.
        if (!e.assisted && e.firstDeathTick >= 0 && age >= e.firstDeathTick + 4) {
            profile.notePessimism(e.mode, e.x);
            e.resolved = true;
            continue;
        }

        // Survival past horizon and past the spot's X.
        if (e.assisted && age >= e.horizon) {
            float const px = player->getPositionX();
            if (px > e.x + 10.f) {
                profile.noteSaved(e.mode, e.x);
            }
            e.resolved = true;
        }
    }
}

void AssistController::runPostmortem(
    PlayLayer* playLayer, PlayerObject* player, GameObject* object, DecisionEntry& dec
) {
    auto& profile = LevelProfile::get();
    auto& sim = ShadowSimulator::get();

    int const age = m_attemptTick - dec.tick;
    int const maxTicks = std::max(8, age + 4);

    // Replay with real inputs from the snapshot.
    auto rep = sim.replay(
        playLayer, dec.snapshotSlot, dec.tick, dec.timeline, m_simDt, maxTicks, false, 0
    );

    bool const tickMatch = rep.died && std::abs(rep.deathTick - age) <= 1;
    bool const objMatch = !object || !rep.deathObject || object == rep.deathObject;
    bool const reproduced = tickMatch && objMatch;

    if (!reproduced) {
        profile.noteTrust(dec.mode, dec.x, 0.6f, "sim diverged");
        profile.noteDiedAfterAssist(dec.mode, dec.x);
        dec.resolved = true;
        return;
    }

    // Sim matches reality. Prefer d+1 first — "too early" is the common failure.
    if (dec.assisted && dec.delay > 0) {
        bool improved = false;
        if (dec.delay < dec.limit) {
            auto late = sim.replay(
                playLayer, dec.snapshotSlot, dec.tick, dec.timeline,
                m_simDt, maxTicks, true, +1
            );
            if (!late.died) {
                profile.noteBias(dec.mode, dec.x, +0.5f, "d+1 survived");
                improved = true;
            }
        }
        if (!improved && dec.delay >= 1) {
            auto early = sim.replay(
                playLayer, dec.snapshotSlot, dec.tick, dec.timeline,
                m_simDt, maxTicks, true, -1
            );
            if (!early.died) {
                profile.noteBias(dec.mode, dec.x, -0.5f, "d-1 survived");
                improved = true;
            }
        }
        if (!improved) {
            profile.noteDiedAfterAssist(dec.mode, dec.x);
        }
    }
    else {
        profile.noteDiedAfterAssist(dec.mode, dec.x);
    }

    dec.resolved = true;
}

void AssistController::onRealDeath(PlayLayer* playLayer, PlayerObject* player, GameObject* object) {
    if (!playLayer || !player) return;
    if (ShadowSimulator::get().isSimulating()) return;
    if (ShadowSimulator::get().isShadow(player)) return;

    if (StageConfig::autoTune() && LevelProfile::get().loaded()) {
        if (auto* dec = m_log.findForDeath(m_attemptTick)) {
            runPostmortem(playLayer, player, object, *dec);
        }
        LevelProfile::get().maybeAutosave();
    }

    clearBuffer();
    m_pendingRelease = false;
    syncButtonState(playLayer);
    updateOverlay();
}

void AssistController::flushPendingRelease(PlayLayer* playLayer) {
    if (!m_pendingRelease || !playLayer) return;
    if (m_attemptTick < m_pendingReleaseTick) return;
    bool const p1 = m_pendingReleaseP1;
    m_pendingRelease = false;
    if (userHolding(p1)) return;
    m_injecting = true;
    playLayer->handleButton(false, JUMP_BUTTON, p1);
    m_injecting = false;
}

void AssistController::fireBuffered(PlayLayer* playLayer, PlayerObject* player) {
    // Last look before inject: if "press now" still dies, slide later inside the
    // original window. No blind +1 — only shift when the sim still says early.
    int const elapsed = m_attemptTick - m_buffer.pressTick;
    int const remaining = m_buffer.limit - elapsed;
    if (player && remaining > 0 && m_buffer.rechecks < 2 && m_simDt > 0.f) {
        PlanOptions opts;
        opts.budgetUs = StageConfig::simBudgetUs();
        opts.capMul = StageConfig::autoTune()
            ? LevelProfile::get().capMul(m_buffer.mode)
            : 1.f;
        opts.maxBranches = 2;
        auto re = ShadowSimulator::get().plan(
            playLayer, player, m_buffer.down, remaining, m_simDt, m_simDt, opts
        );
        if (re.action == PlanAction::Delay && re.delay > 0) {
            int const newFire = m_attemptTick + re.delay;
            if (newFire <= m_buffer.pressTick + m_buffer.limit) {
                ++m_buffer.rechecks;
                m_buffer.fireTick = newFire;
                AssistLog::debug(fmt::format(
                    "recheck: still early, +{} more (tick={})", re.delay, m_attemptTick
                ));
                return;
            }
        }
    }

    bool const wasPress = m_buffer.down;
    bool const player1 = m_buffer.player1;
    bool const letGo = m_buffer.releaseSeen || !userHolding(player1);
    int const delayed = std::max(0, m_attemptTick - m_buffer.pressTick);

    inject(playLayer, m_buffer.down);
    AssistLog::printSavedPress(delayed, m_buffer.pressTick, m_attemptTick, m_buffer.limit, estimatedTps());
    if (!wasPress) {
        AssistLog::debug(fmt::format("(assisted RELEASE, tick={})", m_attemptTick));
    }
    clearBuffer();

    if (wasPress && letGo) {
        m_pendingRelease = true;
        m_pendingReleaseP1 = player1;
        m_pendingReleaseTick = m_attemptTick + 1;
    }
}

void AssistController::onPhysicsTick(PlayLayer* playLayer, float dt) {
    if (dt > 0.f) {
        if (m_attemptTick < 5 || dt > 0.6f * m_lastDt) m_lastDt = dt;
        float tw = playLayer ? playLayer->m_gameState.m_timeWarp : 1.f;
        if (tw <= 0.f) tw = 1.f;
        m_simDt = m_lastDt / tw;
    }
    ++m_attemptTick;
    if (!playLayer) return;

    flushPendingRelease(playLayer);

    auto* p1 = playLayer->m_player1;
    if (p1) {
        m_lastPlayerX = p1->getPositionX();
        m_lastMode = StageConfig::kindOf(p1);

        bool const modeChanged = m_modeKnown && (
            m_modeShip != p1->m_isShip ||
            m_modeBall != p1->m_isBall ||
            m_modeBird != p1->m_isBird ||
            m_modeDart != p1->m_isDart ||
            m_modeRobot != p1->m_isRobot ||
            m_modeSpider != p1->m_isSpider ||
            m_modeSwing != p1->m_isSwing
        );
        m_modeShip = p1->m_isShip;
        m_modeBall = p1->m_isBall;
        m_modeBird = p1->m_isBird;
        m_modeDart = p1->m_isDart;
        m_modeRobot = p1->m_isRobot;
        m_modeSpider = p1->m_isSpider;
        m_modeSwing = p1->m_isSwing;
        m_modeKnown = true;

        if (modeChanged && m_buffer.state != BufferState::Idle) {
            AssistLog::debug("mode change → delivering buffered input");
            fireBuffered(playLayer, p1);
            updateOverlay();
        }

        tickLearning(playLayer, p1);
    }

    if (m_buffer.state == BufferState::Idle) {
        updateOverlay();
        return;
    }

    auto* player = m_buffer.player1 ? playLayer->m_player1 : playLayer->m_player2;
    if (!player || player->m_isDead) {
        clearBuffer();
        syncButtonState(playLayer);
        updateOverlay();
        return;
    }

    if (m_buffer.state == BufferState::Buffered) {
        if (m_attemptTick >= m_buffer.fireTick) {
            fireBuffered(playLayer, player);
        }
    }
    else if (m_buffer.state != BufferState::Idle) {
        clearBuffer();
    }
    updateOverlay();
}

bool AssistController::planPress(PlayLayer* playLayer, PlayerObject* player, bool player1, int effectTick, float firstDt) {
    int const limit = activeLimit(player);
    if (limit <= 0) return true;

    auto const kind = StageConfig::kindOf(player);
    float const x = player->getPositionX();
    auto& profile = LevelProfile::get();

    // Low trust at this spot → never delay, just pass through.
    if (StageConfig::autoTune() && profile.loaded()) {
        if (auto const* spot = profile.trySpot(kind, x)) {
            if (spot->n >= LevelProfile::TRUST_PASS_MIN_N
                && spot->trust < LevelProfile::TRUST_PASS_THRESHOLD) {
                AssistLog::debug(fmt::format(
                    "learned: low trust @{} (trust {:.2f}, n={})",
                    LevelProfile::spotBucket(x), spot->trust, spot->n
                ));
                SimPlan dummy;
                dummy.action = PlanAction::Pass;
                dummy.firstDeathTick = -1;
                recordDecision(playLayer, player, true, dummy, effectTick, 0, limit);
                return true;
            }
        }
    }

    PlanOptions opts;
    opts.budgetUs = StageConfig::simBudgetUs();
    opts.capMul = StageConfig::autoTune() ? profile.capMul(kind) : 1.f;
    opts.maxBranches = 4;

    auto plan = ShadowSimulator::get().plan(playLayer, player, true, limit, m_simDt, firstDt, opts);

    if (plan.action != PlanAction::Delay) {
        AssistLog::debug(fmt::format(
            "passthrough press ({}{}limit {}) tick={}",
            plan.unknown ? "portal, " : "",
            plan.nowIsSafe ? "safe, " : "no assist, ",
            limit, effectTick
        ));
        recordDecision(playLayer, player, true, plan, effectTick, 0, limit);
        return true;
    }

    // Bias may shift the delay, but must never turn a real Delay into Pass
    // (negative learn bias was wiping the assist entirely).
    int delay = plan.delay;
    if (StageConfig::autoTune() && profile.loaded()) {
        if (auto const* spot = profile.trySpot(kind, x)) {
            delay = delay + roundBias(spot->bias);
        }
        delay = std::max(1, std::min(limit, delay));
    }

    m_buffer = {};
    m_buffer.state = BufferState::Buffered;
    m_buffer.down = true;
    m_buffer.player1 = player1;
    m_buffer.pressTick = effectTick;
    m_buffer.fireTick = effectTick + delay;
    m_buffer.limit = limit;
    m_buffer.expectedJumpTick = effectTick + plan.jumpTick;
    m_buffer.continuous = plan.continuous;
    m_buffer.decisionX = x;
    m_buffer.mode = kind;

    AssistLog::debug(fmt::format(
        "ASSIST press +{} ticks (now dies @+{}, limit {}, tick={})",
        delay, plan.firstDeathTick, limit, effectTick
    ));
    recordDecision(playLayer, player, true, plan, effectTick, delay, limit);
    updateOverlay();
    return false;
}

bool AssistController::planRelease(PlayLayer* playLayer, PlayerObject* player, bool player1, int effectTick, float firstDt) {
    int const limit = activeLimit(player);
    if (limit <= 0) return true;

    auto const kind = StageConfig::kindOf(player);
    float const x = player->getPositionX();
    auto& profile = LevelProfile::get();

    if (StageConfig::autoTune() && profile.loaded()) {
        if (auto const* spot = profile.trySpot(kind, x)) {
            if (spot->n >= LevelProfile::TRUST_PASS_MIN_N
                && spot->trust < LevelProfile::TRUST_PASS_THRESHOLD) {
                AssistLog::debug(fmt::format(
                    "learned: low trust @{} (release)", LevelProfile::spotBucket(x)
                ));
                return true;
            }
        }
    }

    PlanOptions opts;
    opts.budgetUs = StageConfig::simBudgetUs();
    opts.capMul = StageConfig::autoTune() ? profile.capMul(kind) : 1.f;
    opts.maxBranches = 4;

    auto plan = ShadowSimulator::get().plan(playLayer, player, false, limit, m_simDt, firstDt, opts);

    if (plan.action != PlanAction::Delay) {
        recordDecision(playLayer, player, false, plan, effectTick, 0, limit);
        return true;
    }

    int delay = plan.delay;
    if (StageConfig::autoTune() && profile.loaded()) {
        if (auto const* spot = profile.trySpot(kind, x)) {
            delay = delay + roundBias(spot->bias);
        }
        delay = std::max(1, std::min(limit, delay));
    }

    m_buffer = {};
    m_buffer.state = BufferState::Buffered;
    m_buffer.down = false;
    m_buffer.player1 = player1;
    m_buffer.pressTick = effectTick;
    m_buffer.fireTick = effectTick + delay;
    m_buffer.limit = limit;
    m_buffer.expectedJumpTick = effectTick + delay;
    m_buffer.continuous = plan.continuous;
    m_buffer.decisionX = x;
    m_buffer.mode = kind;

    AssistLog::debug(fmt::format(
        "ASSIST release +{} ticks (now dies @+{}, limit {}, tick={})",
        delay, plan.firstDeathTick, limit, effectTick
    ));
    recordDecision(playLayer, player, false, plan, effectTick, delay, limit);
    updateOverlay();
    return false;
}

bool AssistController::onHandleButton(
    GJBaseGameLayer* layer,
    bool down,
    int button,
    bool player1
) {
    if (m_injecting) return true;
    if (button != JUMP_BUTTON) return true;

    auto* playLayer = typeinfo_cast<PlayLayer*>(layer);
    if (!playLayer) return true;

    auto* player = player1 ? playLayer->m_player1 : playLayer->m_player2;
    if (!player) return true;

    m_userHolding[player1 ? 0 : 1] = down;

    bool const midStep = m_inRealUpdate;
    int const effectTick = midStep ? m_attemptTick : m_attemptTick + 1;
    float firstDt = m_simDt;
    if (midStep) {
        float rem = m_lastDt - m_subDtSeen;
        float const minDt = m_lastDt * 0.02f;
        if (rem < minDt) rem = minDt;
        if (rem > m_lastDt) rem = m_lastDt;
        float tw = playLayer->m_gameState.m_timeWarp;
        if (tw <= 0.f) tw = 1.f;
        firstDt = rem / tw;
    }

    // Track real (non-swallowed) inputs for learning timelines.
    // Buffered/swallowed ones are recorded when we inject or as releaseSeen.
    bool const ours = m_buffer.state != BufferState::Idle && m_buffer.player1 == player1;

    if (!down) {
        if (ours) {
            if (!m_buffer.down) {
                return false;
            }
            m_buffer.releaseSeen = true;
            m_buffer.releaseTick = effectTick;
            m_log.noteInput(effectTick, false);
            return false;
        }

        if (!shouldAssistPlayer(player) || !StageConfig::assistRelease()) return true;
        if (m_attemptTick <= 2) return true;
        bool const pass = planRelease(playLayer, player, player1, effectTick, firstDt);
        if (pass) m_log.noteInput(effectTick, false);
        return pass;
    }

    if (ours && m_buffer.state == BufferState::Buffered && m_buffer.down && !m_buffer.releaseSeen) {
        return false;
    }
    if (ours && m_buffer.state == BufferState::Buffered && !m_buffer.down) {
        fireBuffered(playLayer, player);
    }

    if (!shouldAssistPlayer(player) || !StageConfig::assistPress()) return true;
    if (m_attemptTick <= 2) return true;
    bool const pass = planPress(playLayer, player, player1, effectTick, firstDt);
    if (pass) m_log.noteInput(effectTick, true);
    return pass;
}

bool AssistController::allowPushButton(PlayerObject* player, PlayerButton button) {
    if (m_injecting) return true;
    if (button != PlayerButton::Jump) return true;
    if (m_buffer.state != BufferState::Buffered || !m_buffer.down) return true;

    auto* playLayer = PlayLayer::get();
    if (!playLayer || !player) return true;

    bool const isP1 = player == playLayer->m_player1;
    if (player != playLayer->m_player1 && player != playLayer->m_player2) return true;

    return m_buffer.player1 != isP1;
}

}
