#include "AssistController.hpp"
#include "AssistLearning.hpp"
#include "AssistPerfStats.hpp"
#include "AssistScopeGate.hpp"
#include "BakeMap.hpp"
#include "BakeVerifier.hpp"
#include "ClickIntentGuard.hpp"
#include "SelfBakeRecorder.hpp"
#include "DivergenceTracker.hpp"
#include "LevelProfile.hpp"
#include "ShadowSimulator.hpp"
#include <chrono>
#include <cstring>
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"
#include "../ui/SaveFeedback.hpp"
#include "../ui/StatusOverlay.hpp"
#include "../ui/TrajectoryVisualizer.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;


constexpr int JUMP_BUTTON = 1;

static int roundBias(float bias) {
    if (bias >= 0.f) return static_cast<int>(std::lround(bias));
    return static_cast<int>(std::lround(bias));
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

void AssistController::onCheckpointLoad() {
    SelfBakeRecorder::get().flushSegment(PlayLayer::get(), "checkpoint");
    clearBuffer();
    m_pendingRelease = false;
    m_pendingReleaseHold = false;
    m_outcomePending = false;
    ClickIntentGuard::get().reset();
}

void AssistController::onLevelComplete(PlayLayer* playLayer) {
    SelfBakeRecorder::get().flushSegment(playLayer, "complete");
}

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
    SelfBakeRecorder::get().flushSegment(playLayer, "reset");
    clearBuffer();
    m_pendingRelease = false;
    m_pendingReleaseHold = false;
    m_assistTouchedRun = false;
    m_savesAttempt = 0;
    DivergenceTracker::get().clear();
    m_attemptTick = 0;
    m_inRealUpdate = false;
    m_subDtSeen = 0.f;
    m_modeKnown = false;
    m_startSnapshotTaken = false;
    m_startSnapshotSlot = -1;
    m_lastBakeHint.clear();
    m_log.clear();
    m_clickTimes.clear();
    m_cpsLocked = false;
    m_outcomePending = false;
    SaveFeedback::get().reset();
    ClickIntentGuard::get().reset();
    syncButtonState(playLayer);
    updateOverlay();
}

void AssistController::onLevelInit(PlayLayer* playLayer) {
    m_userHolding[0] = m_userHolding[1] = false;
    SelfBakeRecorder::get().onLevelInit(playLayer);
    resetAttempt(playLayer);
    ShadowSimulator::get().attach(playLayer);
    StatusOverlay::attach(playLayer);
    TrajectoryVisualizer::get().setEnabled(StageConfig::showTrajectory());
    TrajectoryVisualizer::get().attach(playLayer);
    if (playLayer && playLayer->m_level) {
        LevelProfile::get().loadForLevel(playLayer->m_level);
        BakeMap::get().loadForLevel(playLayer->m_level);
    }
    updateOverlay();
}

void AssistController::onLevelQuit() {
    // Tear down UI/sim refs BEFORE PlayLayer destroys its node tree.
    // layerDying=true: do not removeFromParent (avoids UAF during quit).
    SelfBakeRecorder::get().flushSegment(PlayLayer::get(), "quit");
    SelfBakeRecorder::get().onLevelQuit();
    SaveFeedback::get().reset();
    ClickIntentGuard::get().reset();
    TrajectoryVisualizer::get().detach(true);
    StatusOverlay::detach(true);
    ShadowSimulator::get().detach(true);

    LevelProfile::get().save();
    BakeMap::get().save();
    LevelProfile::get().clear();

    // Quiet reset — overlay already detached
    clearBuffer();
    m_pendingRelease = false;
    m_attemptTick = 0;
    m_inRealUpdate = false;
    m_subDtSeen = 0.f;
    m_modeKnown = false;
    m_startSnapshotTaken = false;
    m_startSnapshotSlot = -1;
    m_lastBakeHint.clear();
    m_log.clear();
    m_clickTimes.clear();
    m_cpsLocked = false;
    m_userHolding[0] = m_userHolding[1] = false;
}

geode::Result<> AssistController::ensureStartSnapshot(PlayLayer* layer) {
    if (!layer) return geode::Err("No layer");
    if (!m_startSnapshotTaken || m_startSnapshotSlot < 0) {
        if (layer->m_player1 && m_attemptTick <= 5) {
            m_startSnapshotSlot = ShadowSimulator::get().captureSnapshot(layer->m_player1);
            m_startSnapshotTaken = m_startSnapshotSlot >= 0;
        }
    }
    if (!m_startSnapshotTaken) {
        return geode::Err("Reset the level once (start snapshot), then bake");
    }
    return geode::Ok();
}

geode::Result<int> AssistController::runBakeMap(PlayLayer* layer, bool bruteForce) {
    auto snap = ensureStartSnapshot(layer);
    if (snap.isErr()) return geode::Err(snap.unwrapErr());
    auto method = bruteForce ? BakeMethod::BruteForce : BakeMethod::Replay;
    return BakeMap::get().bake(
        layer, m_startSnapshotSlot, StageConfig::bakeRadius(), method
    );
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
    if (cpsLimited()) return false;
    if (!StageConfig::paramsFor(player).enabled) return false;
    if (!AssistScopeGate::allowAssist(PlayLayer::get(), player)) return false;
    return true;
}

void AssistController::pruneClickTimes() {
    auto const now = std::chrono::steady_clock::now();
    while (!m_clickTimes.empty()
        && now - m_clickTimes.front() > std::chrono::seconds(1)) {
        m_clickTimes.pop_front();
    }
}

void AssistController::noteUserClick() {
    m_clickTimes.push_back(std::chrono::steady_clock::now());
    pruneClickTimes();
    int const lim = StageConfig::cpsLimit();
    if (lim > 0 && static_cast<int>(m_clickTimes.size()) >= lim) {
        if (!m_cpsLocked) {
            m_cpsLocked = true;
            AssistLog::info(fmt::format(
                "CPS: limit hit ({} >= {}) — assist paused",
                m_clickTimes.size(), lim
            ));
        }
    }
}

int AssistController::currentCps() const {
    auto const now = std::chrono::steady_clock::now();
    int n = 0;
    for (auto const& t : m_clickTimes) {
        if (now - t <= std::chrono::seconds(1)) ++n;
    }
    return n;
}

bool AssistController::cpsLimited() const {
    int const lim = StageConfig::cpsLimit();
    if (lim <= 0) return false;
    // Use cached lock flag + live count so we unlock as soon as CPS drops.
    return currentCps() >= lim;
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
        m_overlayText = "EarlyBuffer: OFF";
    }
    else {
        int left = 0;
        if (m_buffer.state == BufferState::Buffered) left = m_buffer.fireTick - m_attemptTick;

        auto mode = StageConfig::assistMode();
        auto const* modeName = StageConfig::assistModeName(mode);
        std::string learnBit;
        // Learning only applies to Live / Hybrid fallback — not bake windows.
        if (mode == AssistMode::Replay || mode == AssistMode::BruteForce) {
            learnBit = "learn n/a";
        }
        else if (StageConfig::autoTune()) {
            auto spot = LevelProfile::get().overlaySpot(m_lastMode, m_lastPlayerX);
            learnBit = fmt::format("learn bias{:+d} t{:.1f}", roundBias(spot.bias), spot.trust);
        }
        else {
            learnBit = "learn OFF";
        }
        std::string bakeBit = m_lastBakeHint.empty() ? BakeMap::get().statusLine() : m_lastBakeHint;
        std::string trajBit = TrajectoryVisualizer::get().statusHint();
        int const smartDepth = StageConfig::smartDepth();
        std::string smartBit;
        if (smartDepth > 1
            && (mode == AssistMode::Live || mode == AssistMode::Hybrid)) {
            smartBit = fmt::format("smart {}j/{}c", smartDepth, StageConfig::smartCombos());
        }
        int const cpsLim = StageConfig::cpsLimit();
        std::string cpsBit;
        if (cpsLim > 0) {
            int const cps = currentCps();
            cpsBit = cpsLimited()
                ? fmt::format("CPS LOCK {}/{}", cps, cpsLim)
                : fmt::format("cps {}/{}", cps, cpsLim);
        }
        m_overlayText = fmt::format(
            "EarlyBuffer {} | mode {} | {} left={} | {} | {}",
            StageConfig::stageLabel(stage),
            modeName,
            state,
            left,
            learnBit,
            bakeBit
        );
        if (!smartBit.empty()) m_overlayText += " | " + smartBit;
        if (!cpsBit.empty()) m_overlayText += " | " + cpsBit;
        if (!trajBit.empty()) m_overlayText += " | " + trajBit;
        if (StageConfig::assistOrbs()) m_overlayText += " | orbs";
        if (StageConfig::assistVerifyAb()) m_overlayText += " | VERIFY AB ON";
        if (!StageConfig::safeMode() && StageConfig::isEnabled()) m_overlayText += " | UNSAFE";
        if (auto* pl = PlayLayer::get()) {
            if (AssistScopeGate::isPlatformer(pl->m_player1)) {
                m_overlayText += " | platformer: off";
            }
            if (auto const* reason = AssistScopeGate::blockReason(pl, pl->m_player1)) {
                if (std::strcmp(reason, "percent-disable") == 0) {
                    m_overlayText += fmt::format(" | pct-off @{:.2f}%", pl->getCurrentPercent());
                }
                else if (std::strcmp(reason, "percent-gate") == 0) {
                    m_overlayText += " | pct-gate";
                }
            }
        }
        {
            auto bit = AssistPerfStats::get().overlayBit();
            if (!bit.empty()) m_overlayText += " | " + bit;
        }
        {
            auto bit = DivergenceTracker::get().overlayBit();
            if (!bit.empty()) m_overlayText += " | " + bit;
        }
        if (m_savesAttempt > 0) {
            m_overlayText += fmt::format(" | saves {}/{}", m_savesAttempt, m_savesLevel);
        }
        if (m_buffer.requireOrb && m_buffer.dMin >= 0) {
            m_overlayText += fmt::format(" | orbWin[{},{}]", m_buffer.dMin, m_buffer.dMax);
        }
    }
    StatusOverlay::update(m_overlayText);
}

void AssistController::inject(GJBaseGameLayer* layer, bool down) {
    if (!layer) return;
    m_assistTouchedRun = true;
    if (down) {
        ++m_savesAttempt;
        ++m_savesLevel;
    }
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

void AssistController::beginOutcome(SimPlan const& plan, int effectTick) {
    m_outcomePending = true;
    m_outcomePlanId = plan.planId;
    m_outcomeUntil = effectTick + 96;
    m_outcomeExpectOrb = plan.requireOrb || plan.activatedRing;
    m_outcomeStartX = m_lastPlayerX;
}

void AssistController::pollOutcome(PlayLayer* playLayer) {
    if (!m_outcomePending || !playLayer) return;
    auto* p = m_outcomePlayer1 ? playLayer->m_player1 : playLayer->m_player2;
    if (!p) {
        m_outcomePending = false;
        return;
    }
    if (p->m_isDead) {
        AssistLog::debug(fmt::format(
            "OUTCOME id={} death x={:.1f} orbExpect={}",
            m_outcomePlanId, p->getPositionX(), m_outcomeExpectOrb
        ));
        DivergenceTracker::get().note(true, true, m_outcomeExpectOrb, false);
        m_outcomePending = false;
        return;
    }
    if (m_attemptTick >= m_outcomeUntil) {
        bool const orbHit = p->m_hasEverHitRing || p->m_touchedRing;
        AssistLog::debug(fmt::format(
            "OUTCOME id={} alive dx={:.1f} orb={} expect={}",
            m_outcomePlanId, p->getPositionX() - m_outcomeStartX, orbHit, m_outcomeExpectOrb
        ));
        DivergenceTracker::get().note(true, false, m_outcomeExpectOrb, orbHit);
        m_outcomePending = false;
    }
}

void AssistController::logPlan(SimPlan const& plan, char const* kind) {
    AssistLog::debug(fmt::format(
        "PLAN id={} kind={} action={} delay={} requireOrb={} hold={} dMin={} dMax={} budgetHit={}",
        plan.planId, kind,
        plan.action == PlanAction::Pass ? "Pass" : "Delay",
        plan.delay, plan.requireOrb, plan.requireHold, plan.dMin, plan.dMax, plan.budgetHit
    ));
}

void AssistController::onRealDeath(PlayLayer* playLayer, PlayerObject* player, GameObject* object) {
    if (!playLayer || !player) return;
    if (ShadowSimulator::get().isSimulating()) return;
    if (ShadowSimulator::get().isOwnedGhost(player)) return;

    if (m_outcomePending) {
        AssistLog::debug(fmt::format(
            "OUTCOME id={} death x={:.1f} orbExpect={}",
            m_outcomePlanId, player->getPositionX(), m_outcomeExpectOrb
        ));
        m_outcomePending = false;
    }

    if (StageConfig::autoTune() && LevelProfile::get().loaded()) {
        if (auto* dec = m_log.findForDeath(m_attemptTick)) {
            runPostmortem(playLayer, player, object, *dec);
        }
        LevelProfile::get().maybeAutosave();
    }

    SelfBakeRecorder::get().flushSegment(playLayer, "death");
    clearBuffer();
    m_pendingRelease = false;
    syncButtonState(playLayer);
    updateOverlay();
}

void AssistController::flushPendingRelease(PlayLayer* playLayer) {
    if (!m_pendingRelease || !playLayer) return;
    if (m_attemptTick < m_pendingReleaseTick) return;
    if (m_pendingReleaseHold) {
        auto* p = m_pendingReleaseP1 ? playLayer->m_player1 : playLayer->m_player2;
        if (p && p->m_isDashing && m_attemptTick < m_pendingReleaseTick + 90) {
            return;
        }
    }
    bool const p1 = m_pendingReleaseP1;
    m_pendingRelease = false;
    m_pendingReleaseHold = false;
    if (userHolding(p1)) return;
    m_injecting = true;
    playLayer->handleButton(false, JUMP_BUTTON, p1);
    m_injecting = false;
}

void AssistController::fireBuffered(PlayLayer* playLayer, PlayerObject* player) {
    int const elapsed = m_attemptTick - m_buffer.pressTick;
    int const remaining = m_buffer.limit - elapsed;

    // Orb/window-aware recheck: stay inside [dMin, dMax] relative to pressTick
    if (player && remaining > 0 && m_buffer.rechecks < 2 && m_simDt > 0.f) {
        int const absMin = m_buffer.dMin >= 0 ? m_buffer.pressTick + m_buffer.dMin : m_buffer.pressTick;
        int const absMax = m_buffer.dMax >= 0
            ? m_buffer.pressTick + m_buffer.dMax
            : m_buffer.pressTick + m_buffer.limit;

        if (m_attemptTick < absMin) {
            // Too early for orb window — wait
            m_buffer.fireTick = absMin;
            ++m_buffer.rechecks;
            return;
        }
        if (m_attemptTick > absMax && m_buffer.requireOrb) {
            // Past window — fire immediately
        }
        else if (!m_buffer.requireOrb || m_attemptTick <= absMax) {
            PlanOptions opts;
            opts.budgetUs = StageConfig::simBudgetUs();
            opts.tickBudget = StageConfig::tickBudgetPerPress() / 4;
            opts.capMul = StageConfig::autoTune()
                ? LevelProfile::get().capMul(m_buffer.mode)
                : 1.f;
            opts.maxBranches = 2;
            opts.smartDepth = 1;
            auto re = ShadowSimulator::get().plan(
                playLayer, player, m_buffer.down, remaining, m_simDt, m_simDt, opts
            );
            if (re.action == PlanAction::Delay && re.delay > 0) {
                int newFire = m_attemptTick + re.delay;
                if (m_buffer.dMin >= 0) newFire = std::max(newFire, absMin);
                if (m_buffer.dMax >= 0) newFire = std::min(newFire, absMax);
                if (newFire > m_attemptTick && newFire <= m_buffer.pressTick + m_buffer.limit) {
                    ++m_buffer.rechecks;
                    m_buffer.fireTick = newFire;
                    AssistLog::debug(fmt::format(
                        "recheck: slide fire={} (tick={})", newFire, m_attemptTick
                    ));
                    return;
                }
            }
        }
    }

    bool const wasPress = m_buffer.down;
    bool const player1 = m_buffer.player1;
    bool const requireHold = m_buffer.requireHold;
    bool const letGo = m_buffer.releaseSeen || !userHolding(player1);
    int const delayed = std::max(0, m_attemptTick - m_buffer.pressTick);
    int const planId = m_buffer.planId;
    bool const expectOrb = m_buffer.requireOrb;

    inject(playLayer, m_buffer.down);
    AssistLog::printSavedPress(delayed, m_buffer.pressTick, m_attemptTick, m_buffer.limit, estimatedTps());
    if (delayed > 0 && std::strcmp(StageConfig::saveFeedbackMode(), "off") != 0) {
        SaveFeedback::get().onSave(playLayer, player, delayed);
    }
    if (!wasPress) {
        AssistLog::debug(fmt::format("(assisted RELEASE, tick={})", m_attemptTick));
    }

    m_outcomePending = true;
    m_outcomePlanId = planId;
    m_outcomeUntil = m_attemptTick + 96;
    m_outcomePlayer1 = player1;
    m_outcomeExpectOrb = expectOrb;
    m_outcomeStartX = player ? player->getPositionX() : 0.f;

    clearBuffer();

    if (wasPress && letGo) {
        m_pendingRelease = true;
        m_pendingReleaseP1 = player1;
        m_pendingReleaseHold = requireHold;
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

    pollOutcome(playLayer);
    flushPendingRelease(playLayer);
    SaveFeedback::get().onPhysicsTick();

    auto* p1 = playLayer->m_player1;
    if (p1) {
        m_lastPlayerX = p1->getPositionX();
        m_lastMode = StageConfig::kindOf(p1);
        // Capture attempt-start physics for Bake Map (once per attempt).
        if (!m_startSnapshotTaken && m_attemptTick <= 2) {
            m_startSnapshotSlot = ShadowSimulator::get().captureSnapshot(p1);
            m_startSnapshotTaken = m_startSnapshotSlot >= 0;
        }

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

    TrajectoryVisualizer::get().onPhysicsTick(playLayer, dt);

    // CPS limiter: unlock when rate drops; flush any held buffer so clicks aren't eaten.
    pruneClickTimes();
    int const cpsLim = StageConfig::cpsLimit();
    if (cpsLim > 0) {
        bool const limited = currentCps() >= cpsLim;
        if (m_cpsLocked && !limited) {
            m_cpsLocked = false;
            AssistLog::info(fmt::format("CPS: unlocked (now {}/{})", currentCps(), cpsLim));
        }
        if (limited && m_buffer.state == BufferState::Buffered && p1) {
            fireBuffered(playLayer, p1);
        }
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

bool AssistController::tryBakeAssist(
    PlayLayer* playLayer, PlayerObject* player, bool player1,
    bool down, int effectTick, float firstDt, bool& handled
) {
    handled = false;
    auto mode = StageConfig::assistMode();
    if (mode == AssistMode::Live) return true;

    bool const bakeOnly =
        mode == AssistMode::Replay || mode == AssistMode::BruteForce;
    bool const hybrid = mode == AssistMode::Hybrid;

    auto fallThroughOrPass = [&](char const* reason) -> bool {
        AssistLog::debug(fmt::format("bake: {} → {}", reason, hybrid ? "live" : "pass"));
        if (hybrid) {
            handled = false;
            m_lastBakeHint = reason;
            return true;
        }
        handled = true;
        m_lastBakeHint = reason;
        return true;
    };

    auto const& bake = BakeMap::get();
    if (!bake.loaded() || bake.data().clicks.empty()) {
        if (bakeOnly) {
            handled = true;
            m_lastBakeHint = mode == AssistMode::BruteForce ? "brute missing" : "bake missing";
            return true; // pass through — nothing to assist with
        }
        return true; // Hybrid → live
    }

    double tps = bake.data().tps > 0.0 ? bake.data().tps : 240.0;
    int approxFrame = static_cast<int>(std::lround(m_attemptTick * (tps * m_simDt)));
    auto const kind = StageConfig::kindOf(player);
    auto const* win = bake.findNear(
        player->getPositionX(), approxFrame, down, !player1, kind
    );
    if (!win) {
        if (bakeOnly) {
            handled = true;
            m_lastBakeHint = "no window";
            return true;
        }
        return true; // Hybrid live fallback
    }

    // Map bake frames onto attempt ticks using bake TPS vs current sim dt.
    auto frameToTick = [&](int frame) -> int {
        if (m_simDt <= 0.f) return effectTick + (frame - win->nominalFrame);
        double ticksPerBakeFrame = 1.0 / (tps * m_simDt);
        int delta = static_cast<int>(std::lround((frame - win->nominalFrame) * ticksPerBakeFrame));
        return effectTick + delta;
    };

    int earliestTick = frameToTick(win->earliest);
    int latestTick = frameToTick(win->latest);
    if (latestTick < earliestTick) std::swap(latestTick, earliestTick);

    int const limit = activeLimit(player);

    // Too late for this window
    if (effectTick > latestTick) {
        return fallThroughOrPass("past latest");
    }

    // Planned delay: 0 if already inside window, else wait until earliest.
    int delay = 0;
    if (effectTick < earliestTick) {
        delay = earliestTick - effectTick;
        if (limit > 0) delay = std::min(delay, limit);
        if (delay <= 0) delay = 0;
    }

    // Live re-verify: stale / wrong-mode / route drift → Hybrid live, bake-only Pass.
    if (limit > 0 && m_simDt > 0.f) {
        float const fd = (firstDt > 0.f && firstDt <= m_simDt) ? firstDt : m_simDt;
        auto const vr = BakeVerifier::verify(
            playLayer, player, down, delay, limit, m_simDt, fd
        );
        if (!vr.ok) {
            return fallThroughOrPass(vr.budgetHit ? "verify budget" : "verify fail");
        }
    }

    handled = true;
    m_lastBakeHint = fmt::format("bake [{},{}]", win->earliest, win->latest);

    // Already inside the safe window → do it now.
    if (delay <= 0) {
        AssistLog::debug(fmt::format(
            "bake: in window [{},{}] verify ok → pass", win->earliest, win->latest
        ));
        return true;
    }

    // Too early → buffer until earliest
    m_buffer = {};
    m_buffer.state = BufferState::Buffered;
    m_buffer.down = down;
    m_buffer.player1 = player1;
    m_buffer.pressTick = effectTick;
    m_buffer.fireTick = effectTick + delay;
    m_buffer.limit = std::max(limit, delay);
    m_buffer.expectedJumpTick = m_buffer.fireTick;
    m_buffer.decisionX = player->getPositionX();
    m_buffer.mode = kind;

    // G4 bake heuristic (no file format change): if Orb Assist would have intent
    // near this bake delay, apply the same [dMin,dMax] recheck/requireOrb rules.
    if (down && StageConfig::assistOrbs()) {
        int const dMin = std::max(0, earliestTick - effectTick);
        int const dMax = std::max(dMin, latestTick - effectTick);
        bool const nearOrb = ShadowSimulator::spatialOrbHint(
            playLayer, player, std::max(limit, dMax + 2), m_simDt
        );
        if (nearOrb) {
            m_buffer.requireOrb = true;
            m_buffer.dMin = dMin;
            m_buffer.dMax = dMax;
            m_buffer.fireTick = std::max(m_buffer.fireTick, effectTick + dMin);
            m_buffer.fireTick = std::min(m_buffer.fireTick, effectTick + dMax);
            AssistLog::debug(fmt::format(
                "bake orb-heuristic win=[{},{}] ticks", dMin, dMax
            ));
        }
    }

    AssistLog::debug(fmt::format(
        "bake ASSIST {} +{} ticks (window [{},{}])",
        down ? "press" : "release", delay, win->earliest, win->latest
    ));
    updateOverlay();
    return false;
}

bool AssistController::planPress(PlayLayer* playLayer, PlayerObject* player, bool player1, int effectTick, float firstDt) {
    int const limit = activeLimit(player);
    if (limit <= 0) return true;

    bool bakeHandled = false;
    bool const bakeRet = tryBakeAssist(
        playLayer, player, player1, true, effectTick, firstDt, bakeHandled
    );
    if (bakeHandled) return bakeRet;

    auto const kind = StageConfig::kindOf(player);
    float const x = player->getPositionX();
    auto& profile = LevelProfile::get();
    int const slot = player1 ? 0 : 1;

    // Dense spam (esp. wave): Pass unless an orb/pad is clearly ahead.
    if (ClickIntentGuard::get().shouldPassThrough(slot, effectTick, kind)) {
        bool nearOrb = false;
        if (StageConfig::assistOrbs() && limit > 0 && m_simDt > 0.f) {
            nearOrb = ShadowSimulator::spatialOrbHint(playLayer, player, limit, m_simDt);
        }
        if (!nearOrb) {
            AssistLog::debug(fmt::format(
                "intent: spam Pass (mode={}, tick={})", StageConfig::kindName(kind), effectTick
            ));
            SimPlan dummy;
            dummy.action = PlanAction::Pass;
            dummy.firstDeathTick = -1;
            dummy.continuous = player->m_isDart || player->m_isShip || player->m_isSwing;
            recordDecision(playLayer, player, true, dummy, effectTick, 0, limit);
            return true;
        }
    }

    // Low trust at this spot → pass through (unless explore roll).
    if (StageConfig::autoTune() && profile.loaded()) {
        if (auto const* spot = profile.trySpot(kind, x)) {
            if (spot->n >= LevelProfile::TRUST_PASS_MIN_N
                && spot->trust < LevelProfile::TRUST_PASS_THRESHOLD
                && !AssistLearning::shouldExploreDespiteLowTrust(spot->trust)) {
                AssistLog::debug(fmt::format(
                    "learned: low trust @{} (trust {:.2f}, n={})",
                    LevelProfile::spotBucket(x), spot->trust, spot->n
                ));
                AssistLearning::decaySpot(kind, x);
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
    opts.tickBudget = StageConfig::tickBudgetPerPress();
    opts.capMul = StageConfig::autoTune() ? profile.capMul(kind) : 1.f;
    opts.maxBranches = 4;
    opts.smartDepth = StageConfig::smartDepth();
    opts.smartCombos = StageConfig::smartCombos();
    opts.smartHorizon = StageConfig::smartHorizon();
    opts.attemptStartTick = effectTick;

    auto const t0 = std::chrono::steady_clock::now();
    auto plan = (opts.smartDepth > 1)
        ? ShadowSimulator::get().planSequence(playLayer, player, true, limit, m_simDt, firstDt, opts)
        : ShadowSimulator::get().plan(playLayer, player, true, limit, m_simDt, firstDt, opts);
    int const planUs = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0
    ).count());
    AssistPerfStats::get().notePlan(planUs, plan.budgetHit);

    logPlan(plan, "press");
    m_outcomePlayer1 = player1;
    beginOutcome(plan, effectTick);

    if (plan.action != PlanAction::Delay) {
        recordDecision(playLayer, player, true, plan, effectTick, 0, limit);
        return true;
    }

    int delay = plan.delay;
    if (StageConfig::autoTune() && profile.loaded()) {
        if (auto const* spot = profile.trySpot(kind, x)) {
            delay = delay + roundBias(spot->bias);
        }
        // Clamp bias into activation window when requireOrb
        if (plan.requireOrb && plan.dMin >= 0 && plan.dMax >= plan.dMin) {
            delay = std::max(plan.dMin, std::min(plan.dMax, delay));
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
    m_buffer.requireOrb = plan.requireOrb;
    m_buffer.requireHold = plan.requireHold;
    m_buffer.dMin = plan.dMin;
    m_buffer.dMax = plan.dMax;
    m_buffer.planId = plan.planId;

    // Keep fire inside activation window
    if (plan.dMin >= 0) {
        m_buffer.fireTick = std::max(m_buffer.fireTick, effectTick + plan.dMin);
    }
    if (plan.dMax >= 0) {
        m_buffer.fireTick = std::min(m_buffer.fireTick, effectTick + plan.dMax);
    }

    AssistLog::debug(fmt::format(
        "ASSIST press +{} ticks id={} ring={} hold={} win=[{},{}]",
        delay, plan.planId, plan.activatedRing, plan.requireHold, plan.dMin, plan.dMax
    ));
    recordDecision(playLayer, player, true, plan, effectTick, delay, limit);
    updateOverlay();
    return false;
}

bool AssistController::planRelease(PlayLayer* playLayer, PlayerObject* player, bool player1, int effectTick, float firstDt) {
    int const limit = activeLimit(player);
    if (limit <= 0) return true;

    bool bakeHandled = false;
    bool const bakeRet = tryBakeAssist(
        playLayer, player, player1, false, effectTick, firstDt, bakeHandled
    );
    if (bakeHandled) return bakeRet;

    auto const kind = StageConfig::kindOf(player);
    float const x = player->getPositionX();
    auto& profile = LevelProfile::get();
    int const slot = player1 ? 0 : 1;

    // Continuous spam: do not delay releases either (would steal hold state).
    bool const continuous =
        player->m_isDart || player->m_isShip || player->m_isSwing;
    if (continuous && ClickIntentGuard::get().shouldPassThrough(slot, effectTick, kind)) {
        bool nearOrb = false;
        if (StageConfig::assistOrbs() && limit > 0 && m_simDt > 0.f) {
            nearOrb = ShadowSimulator::spatialOrbHint(playLayer, player, limit, m_simDt);
        }
        if (!nearOrb) {
            AssistLog::debug(fmt::format(
                "intent: spam Pass release (mode={}, tick={})",
                StageConfig::kindName(kind), effectTick
            ));
            return true;
        }
    }

    if (StageConfig::autoTune() && profile.loaded()) {
        if (auto const* spot = profile.trySpot(kind, x)) {
            if (spot->n >= LevelProfile::TRUST_PASS_MIN_N
                && spot->trust < LevelProfile::TRUST_PASS_THRESHOLD
                && !AssistLearning::shouldExploreDespiteLowTrust(spot->trust)) {
                AssistLog::debug(fmt::format(
                    "learned: low trust @{} (release)", LevelProfile::spotBucket(x)
                ));
                AssistLearning::decaySpot(kind, x);
                return true;
            }
        }
    }

    PlanOptions opts;
    opts.budgetUs = StageConfig::simBudgetUs();
    opts.tickBudget = StageConfig::tickBudgetPerPress();
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

    auto finish = [&](bool passThrough) -> bool {
        if (passThrough) {
            SelfBakeRecorder::get().noteVanillaInput(
                playLayer, player, player1, down, effectTick, m_simDt
            );
        }
        return passThrough;
    };

    // Min click interval: skip assist only — never block the vanilla click.
    if (down) {
        int const minMs = StageConfig::minClickIntervalMs();
        if (minMs > 0 && !m_clickTimes.empty()) {
            auto const elapsed = std::chrono::steady_clock::now() - m_clickTimes.back();
            if (elapsed < std::chrono::milliseconds(minMs)) {
                AssistLog::debug(fmt::format(
                    "click debounce: no assist ({}ms < {}ms), pass through",
                    std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
                    minMs
                ));
                return finish(true);
            }
        }
        noteUserClick();
        ClickIntentGuard::get().notePress(
            player1 ? 0 : 1, effectTick, StageConfig::kindOf(player)
        );
    }
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

        if (!shouldAssistPlayer(player) || !StageConfig::assistRelease()) return finish(true);
        if (m_attemptTick <= 2) return finish(true);
        bool const pass = planRelease(playLayer, player, player1, effectTick, firstDt);
        if (pass) m_log.noteInput(effectTick, false);
        return finish(pass);
    }

    if (ours && m_buffer.state == BufferState::Buffered && m_buffer.down) {
        if (!m_buffer.releaseSeen) {
            return false; // same press still armed; swallow duplicate downs
        }
        // Press B before fireTick after Release A: fire A first, then plan B.
        fireBuffered(playLayer, player);
    }
    if (ours && m_buffer.state == BufferState::Buffered && !m_buffer.down) {
        fireBuffered(playLayer, player);
    }

    if (!shouldAssistPlayer(player) || !StageConfig::assistPress()) return finish(true);
    if (m_attemptTick <= 2) return finish(true);
    bool const pass = planPress(playLayer, player, player1, effectTick, firstDt);
    if (pass) m_log.noteInput(effectTick, true);
    return finish(pass);
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


