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
#include "../debug/LagProbe.hpp"
#include "../debug/PerfTrace.hpp"
#include "../ui/SaveFeedback.hpp"
#include "../ui/SaveFrameCounter.hpp"
#include "../ui/StatusOverlay.hpp"
#include "../ui/TrajectoryVisualizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace geode::prelude;


constexpr int JUMP_BUTTON = 1;

static int roundBias(float bias) {
    if (bias >= 0.f) return static_cast<int>(std::lround(bias));
    return static_cast<int>(std::lround(bias));
}

static void fillPlanRobustLatency(
    PlanOptions& opts,
    PlayerObject* player,
    float dtVariance,
    bool spamDense = false
) {
    opts.robustK = StageConfig::robustK();
    opts.robustJitter = StageConfig::robustJitter();
    opts.dtVariance = dtVariance;
    opts.spamDense = spamDense;
    if (player) {
        opts.latencyWeight = StageConfig::paramsFor(player).latencyWeight;
        // Wave λ=1.0 prefers earliest hard — in spam+orb assists that phase-shifts
        // into spikes. Soften only on spam-dense assists (Pass-excluded paths).
        if (spamDense && player->m_isDart && opts.latencyWeight > 0.5f) {
            opts.latencyWeight *= 0.65f;
        }
    }
}

// Map a nearby BakeMap window onto delay ticks for Live prefer-band ordering.
static void fillBakePreferBand(
    PlanOptions& opts,
    PlayerObject* player,
    bool player1,
    bool down,
    int effectTick,
    int limit,
    int attemptTick,
    float simDt
) {
    if (!player || limit <= 0 || simDt <= 0.f) return;
    auto const& bake = BakeMap::get();
    if (!bake.loaded() || bake.data().clicks.empty()) return;

    double tps = bake.data().tps > 0.0 ? bake.data().tps : 240.0;
    int const approxFrame = static_cast<int>(std::lround(attemptTick * (tps * simDt)));
    auto const* win = bake.findNear(
        player->getPositionX(), approxFrame, down, !player1,
        StageConfig::kindOf(player)
    );
    if (!win) return;

    auto frameToTick = [&](int frame) -> int {
        double const ticksPerBakeFrame = 1.0 / (tps * simDt);
        int const delta = static_cast<int>(std::lround(
            (frame - win->nominalFrame) * ticksPerBakeFrame
        ));
        return effectTick + delta;
    };

    int e = frameToTick(win->earliest) - effectTick;
    int l = frameToTick(win->latest) - effectTick;
    if (l < e) std::swap(l, e);
    e = std::max(0, std::min(limit, e));
    l = std::max(0, std::min(limit, l));
    if (e > l) return;
    opts.preferDelayMin = e;
    opts.preferDelayMax = l;
}

// Nearby bake click → ClickIntentGuard must not Pass (assist allowed).
static bool nearBakeWindowHint(
    PlayerObject* player,
    bool player1,
    bool down,
    int attemptTick,
    float simDt
) {
    if (!player || simDt <= 0.f) return false;
    auto const& bake = BakeMap::get();
    if (!bake.loaded() || bake.data().clicks.empty()) return false;
    double tps = bake.data().tps > 0.0 ? bake.data().tps : 240.0;
    int const approxFrame = static_cast<int>(std::lround(attemptTick * (tps * simDt)));
    return bake.findNear(
        player->getPositionX(), approxFrame, down, !player1,
        StageConfig::kindOf(player)
    ) != nullptr;
}

static bool nearOrbOrPadHint(
    PlayLayer* playLayer, PlayerObject* player, int limit, float simDt
) {
    if (!StageConfig::assistOrbs()) return false;
    if (!playLayer || !player || limit <= 0 || simDt <= 0.f) return false;
    return ShadowSimulator::spatialOrbHint(playLayer, player, limit, simDt);
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
    // Do not flush/refine here — idle drain handles the buffer after respawn.
    SelfBakeRecorder::get().flushSegment(PlayLayer::get(), "checkpoint", false);
    clearBuffer();
    m_pendingRelease = false;
    m_pendingReleaseHold = false;
    m_outcomePending = false;
    ClickIntentGuard::get().reset();
}

void AssistController::onLevelComplete(PlayLayer* playLayer) {
    SelfBakeRecorder::get().flushMergeOnly(playLayer, "complete");
    LevelProfile::get().save();
    BakeMap::get().save();
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
    // Self-bake stays buffered — idle drain merges after we're alive again.
    LagProbe::Scope const episodeScope("eb.resetAttempt");
    {
        LagProbe::Scope const s("eb.flushSegment");
        SelfBakeRecorder::get().flushSegment(playLayer, "reset", false);
    }
    clearBuffer();
    this->m_pendingRelease = false;
    this->m_pendingReleaseHold = false;
    this->m_pendingReleaseContinuous = false;
    this->m_pendingReleaseTick = 0;
    this->m_pendingReleaseLatest = 0;
    m_assistTouchedRun = false;
    m_savesAttempt = 0;
    DivergenceTracker::get().clear();
    m_attemptTick = 0;
    m_inRealUpdate = false;
    m_subDtSeen = 0.f;
    m_dtEma = 1.f / 240.f;
    m_dtVarEma = 0.f;
    m_modeKnown = false;
    m_startSnapshotTaken = false;
    m_startSnapshotSlot = -1;
    m_lastBakeHint.clear();
    m_log.clear();
    m_clickTimes.clear();
    m_cpsLocked = false;
    m_outcomePending = false;
    m_outcomePlanDelay = false;
    m_untrustPassHint = false;
    {
        LagProbe::Scope const s("eb.saveFeedbackReset");
        SaveFeedback::get().reset();
    }
    {
        LagProbe::Scope const s("eb.clickGuardReset");
        ClickIntentGuard::get().reset();
    }
    {
        LagProbe::Scope const s("eb.syncButtons");
        syncButtonState(playLayer);
    }
    {
        LagProbe::Scope const s("eb.updateOverlay");
        updateOverlay();
    }
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
    // detach() unlinks then releases — never release while still parented.
    SelfBakeRecorder::get().cancelRefine();
    SelfBakeRecorder::get().flushMergeOnly(PlayLayer::get(), "quit");
    SelfBakeRecorder::get().onLevelQuit();
    SaveFeedback::get().reset();
    SaveFrameCounter::get().detach(true);
    ClickIntentGuard::get().reset();
    TrajectoryVisualizer::get().detach(true);
    StatusOverlay::detach(true);
    ShadowSimulator::get().detach(true);

    // Persist then clear. Skip PerfTrace sync flush here — large logs were
    // hitching quits; pending lines flush on next enable / natural batch.
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
    // True only while PlayerObject::update runs for real P1
    // (see GameLayerHooks). CBF / mid-step clicks set midStep=true.
    this->m_inRealUpdate = true;
    this->m_subDtSeen = 0.f;
}

void AssistController::endRealUpdate() {
    this->m_inRealUpdate = false;
}

void AssistController::onSubStep(PlayerObject* player, float dt) {
    if (!this->m_inRealUpdate || dt <= 0.f) return;
    auto* pl = PlayLayer::get();
    if (!pl || player != pl->m_player1) return;
    this->m_subDtSeen += dt;
}

bool AssistController::continuousModeKind(GameModeKind kind) {
    switch (kind) {
        case GameModeKind::Wave:
        case GameModeKind::Ship:
        case GameModeKind::Swing:
        case GameModeKind::Ufo:
        case GameModeKind::Spider:
            return true;
        default:
            return false;
    }
}

bool AssistController::continuousModePlayer(PlayerObject* player) {
    if (!player) return false;
    return continuousModeKind(StageConfig::kindOf(player));
}

int AssistController::resolveEffectTick(int attemptTick, bool midStep, bool continuous) {
    if (attemptTick < 0) attemptTick = 0;
    // Continuous: unify mid-step vs between-frames. The old +1 path for
    // between-frames systematically scheduled fires one tick later than an
    // otherwise identical mid-step press — fatal in wave spam (±1).
    // firstDt still carries CBF remainder when midStep.
    if (continuous) return attemptTick;
    return midStep ? attemptTick : attemptTick + 1;
}

bool AssistController::effectTickSelfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    // Discrete cube-like: mid-step stays on current tick; between frames advances.
    expect(resolveEffectTick(10, true, false) == 10, "discrete-mid");
    expect(resolveEffectTick(10, false, false) == 11, "discrete-between");

    // Continuous: never +1 (mid and between share the same effectTick).
    expect(resolveEffectTick(10, true, true) == 10, "cont-mid");
    expect(resolveEffectTick(10, false, true) == 10, "cont-between");
    expect(resolveEffectTick(0, false, true) == 0, "cont-zero");
    expect(resolveEffectTick(-3, false, false) == 1, "neg-clamp-discrete");
    expect(resolveEffectTick(-3, true, true) == 0, "neg-clamp-cont");

    expect(continuousModeKind(GameModeKind::Wave), "kind-wave");
    expect(continuousModeKind(GameModeKind::Ship), "kind-ship");
    expect(continuousModeKind(GameModeKind::Swing), "kind-swing");
    expect(continuousModeKind(GameModeKind::Ufo), "kind-ufo");
    expect(continuousModeKind(GameModeKind::Spider), "kind-spider");
    expect(!continuousModeKind(GameModeKind::Cube), "kind-cube");
    expect(!continuousModeKind(GameModeKind::Ball), "kind-ball");
    expect(!continuousModeKind(GameModeKind::Robot), "kind-robot");

    // Mid-step must not double-advance: continuous mid == discrete mid == attemptTick.
    expect(
        resolveEffectTick(42, true, true) == resolveEffectTick(42, true, false),
        "mid-no-double-advance"
    );

    if (fails == 0) {
        report = "effectTickSelfCheck OK";
        return true;
    }
    report = fmt::format("effectTickSelfCheck {} fails: {}", fails, report);
    return false;
}

int AssistController::resolveFireTick(
    int effectTick, int delay, int jumpTick, bool requireOrb
) {
    if (effectTick < 0) effectTick = 0;
    if (delay < 0) delay = 0;
    int fireDelay = delay;
    // Non-orb: sim may register the jump after the press tick. Injecting on
    // delay alone then feels one frame early vs when the jump matters.
    // Orb windows: activation timing (delay / dMin) wins — hold covers jump.
    if (!requireOrb && jumpTick > delay) {
        fireDelay = jumpTick;
    }
    return effectTick + fireDelay;
}

bool AssistController::shouldFireBuffered(
    int attemptTick, int fireTick, int pressTick, int planDelay, int limit
) {
    if (attemptTick >= fireTick) return true;
    if (limit <= 0) return attemptTick >= fireTick;

    // Late-fire guard: last tick of the forgiveness window — inject rather
    // than miss entirely when jump alignment pushed fireTick to the edge.
    int const deadline = pressTick + limit;
    if (attemptTick >= deadline) return true;
    if (attemptTick >= deadline - 1 && attemptTick >= pressTick + std::max(0, planDelay)) {
        return true;
    }
    return false;
}

bool AssistController::fireTickSelfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    // Same-tick jump → fire at delay.
    expect(resolveFireTick(100, 3, 3, false) == 103, "jump-eq-delay");
    // Jump one tick later → fire at jumpTick (non-orb).
    expect(resolveFireTick(100, 3, 4, false) == 104, "jump-after-delay");
    // Orb keeps activation delay even if jump lags.
    expect(resolveFireTick(100, 3, 5, true) == 103, "orb-keeps-delay");
    expect(resolveFireTick(0, 0, 0, false) == 0, "zero");
    expect(resolveFireTick(-2, 2, 2, false) == 2, "neg-effect");

    expect(shouldFireBuffered(10, 10, 5, 3, 8), "due-eq");
    expect(shouldFireBuffered(11, 10, 5, 3, 8), "due-late");
    expect(!shouldFireBuffered(9, 10, 5, 3, 8), "not-yet");
    // Last tick of window (press=5, limit=8 → deadline=13): at 12 with planDelay met.
    expect(shouldFireBuffered(12, 14, 5, 3, 8), "last-chance");
    expect(!shouldFireBuffered(11, 14, 5, 3, 8), "not-last-chance");
    expect(shouldFireBuffered(13, 14, 5, 3, 8), "at-deadline");

    if (fails == 0) {
        report = "fireTickSelfCheck OK";
        return true;
    }
    report = fmt::format("fireTickSelfCheck {} fails: {}", fails, report);
    return false;
}

int AssistController::estimateRobustK(PlayerObject* player, float dtVariance) {
    int forced = StageConfig::robustK();
    if (forced > 0) return std::clamp(forced, 1, 3);
    // Recheck path: treat as spam-dense when recent presses look rhythmic.
    bool spamDense = false;
    if (player) {
        auto* pl = PlayLayer::get();
        int const slot = (pl && player == pl->m_player2) ? 1 : 0;
        spamDense = ClickIntentGuard::get().shouldPassThrough(
            slot, AssistController::get().attemptTick(), StageConfig::kindOf(player)
        );
    }
    return ShadowSimulator::autoRobustK(player, dtVariance, spamDense);
}

int AssistController::acceptRecheckSlide(
    int oldFire,
    int attemptTick,
    int proposedFire,
    int absMax,
    int robustK,
    int deadline,
    bool requireOrb,
    bool planOk
) {
    if (!planOk) return -1;
    if (proposedFire <= attemptTick) return -1;

    int const k = std::clamp(robustK, 0, 3);
    int safeMax = absMax;
    if (k > 0) {
        safeMax = absMax - k;
        // Collapsed margin → keep absMax so we can still fire inside the window.
        if (safeMax <= attemptTick) {
            safeMax = absMax;
        }
    }
    proposedFire = std::min(proposedFire, safeMax);
    proposedFire = std::min(proposedFire, deadline);
    if (proposedFire <= attemptTick) return -1;

    // Cap late slides only (earlier rechecks stay useful). Orb may wait for dMin.
    if (!requireOrb && proposedFire > oldFire + kRecheckMaxSlide) {
        proposedFire = oldFire + kRecheckMaxSlide;
    }
    if (proposedFire <= attemptTick) return -1;

    // No-op / already due — caller should inject, not re-arm.
    if (proposedFire == oldFire) return -1;
    return proposedFire;
}

bool AssistController::recheckSlideSelfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    // planOk false → reject
    expect(acceptRecheckSlide(10, 8, 12, 20, 1, 20, false, false) < 0, "reject-bad-plan");
    // Late slide capped at +2
    expect(acceptRecheckSlide(10, 8, 20, 30, 0, 30, false, true) == 12, "cap-slide-plus2");
    // Early slide (earlier than oldFire) allowed
    expect(acceptRecheckSlide(15, 8, 11, 30, 0, 30, false, true) == 11, "early-slide-ok");
    // robustK: absMax 20, k=2 → safeMax 18
    expect(acceptRecheckSlide(10, 8, 19, 20, 2, 30, false, true) == 12, "robust-then-cap");
    // past attempt → reject
    expect(acceptRecheckSlide(10, 12, 12, 20, 0, 20, false, true) < 0, "not-future");
    // orb: allow large late slide (dMin wait)
    expect(acceptRecheckSlide(10, 8, 18, 20, 0, 20, true, true) == 18, "orb-allow-late");
    // same fire → reject (inject instead)
    expect(acceptRecheckSlide(12, 8, 12, 20, 0, 20, false, true) < 0, "noop-reject");
    // deadline clamp
    expect(acceptRecheckSlide(10, 8, 14, 30, 0, 8, false, true) < 0, "deadline-collapse");
    expect(acceptRecheckSlide(10, 8, 14, 30, 0, 13, false, true) == 12, "deadline-cap-slide");

    if (fails == 0) {
        report = "recheckSlideSelfCheck OK";
        return true;
    }
    report = fmt::format("recheckSlideSelfCheck {} fails: {}", fails, report);
    return false;
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
            if (m_untrustPassHint) m_overlayText += " | UNTRUST pass";
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

    int const age = this->m_attemptTick - dec.tick;
    // destroyPlayer hot path — keep this to ONE short replay. Extra d±1 probes
    // (up to 3×96 physics ticks) were hitching deaths after assist.
    constexpr int kPostmortemMaxTicks = 32;
    if (age > 400) {
        profile.noteDiedAfterAssist(dec.mode, dec.x);
        dec.resolved = true;
        return;
    }
    int const maxTicks = std::min(kPostmortemMaxTicks, std::max(8, age + 2));

    auto rep = sim.replay(
        playLayer, dec.snapshotSlot, dec.tick, dec.timeline, this->m_simDt, maxTicks, false, 0
    );

    bool const tickMatch = rep.died && std::abs(rep.deathTick - age) <= 1;
    bool const objMatch = !object || !rep.deathObject || object == rep.deathObject;
    bool const reproduced = tickMatch && objMatch;

    if (!reproduced) {
        profile.noteTrust(dec.mode, dec.x, 0.6f, "sim diverged");
    }
    profile.noteDiedAfterAssist(dec.mode, dec.x);
    if (!dec.assisted) {
        profile.noteEarliness(dec.mode, dec.x, 0, "postmortem pass");
    }

    dec.resolved = true;
}

void AssistController::beginOutcome(SimPlan const& plan, int effectTick) {
    m_outcomePending = true;
    m_outcomePlanId = plan.planId;
    m_outcomeUntil = effectTick + 96;
    m_outcomeExpectOrb = plan.requireOrb || plan.activatedRing;
    m_outcomePlanDelay = plan.action == PlanAction::Delay || plan.delay > 0;
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
            "OUTCOME id={} death x={:.1f} orbExpect={} delay={}",
            m_outcomePlanId, p->getPositionX(), m_outcomeExpectOrb, m_outcomePlanDelay
        ));
        DivergenceTracker::get().note(
            m_outcomePlanDelay, true, m_outcomeExpectOrb, false
        );
        m_outcomePending = false;
        return;
    }
    if (m_attemptTick >= m_outcomeUntil) {
        bool const orbHit = p->m_hasEverHitRing || p->m_touchedRing;
        AssistLog::debug(fmt::format(
            "OUTCOME id={} alive dx={:.1f} orb={} expect={} delay={}",
            m_outcomePlanId, p->getPositionX() - m_outcomeStartX,
            orbHit, m_outcomeExpectOrb, m_outcomePlanDelay
        ));
        DivergenceTracker::get().note(
            m_outcomePlanDelay, false, m_outcomeExpectOrb, orbHit
        );
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
    (void)object;

    LagProbe::Scope const deathScope("eb.onRealDeath");

    // destroyPlayer hot path — no physics replays, no disk, no bake merge.
    // GD physics is main-thread only; "multithread" death work is not viable.
    // Heavy learning/bake runs on resetAttempt (next attempt), not here.
    if (this->m_outcomePending) {
        LagProbe::Scope const s("eb.outcomeNote");
        DivergenceTracker::get().note(
            this->m_outcomePlanDelay, true, this->m_outcomeExpectOrb, false
        );
        this->m_outcomePending = false;
    }

    if (StageConfig::autoTune() && LevelProfile::get().loaded()) {
        if (auto* dec = this->m_log.findForDeath(this->m_attemptTick)) {
            LagProbe::Scope const s("eb.learnDeath", fmt::format(
                "assisted={} delay={} x={:.1f}",
                dec->assisted, dec->delay, dec->x
            ));
            // Bookkeeping only — no ShadowSimulator::replay (was hitching deaths).
            LevelProfile::get().noteDiedAfterAssist(dec->mode, dec->x);
            if (!dec->assisted) {
                LevelProfile::get().noteEarliness(dec->mode, dec->x, 0, "death pass");
            }
            dec->resolved = true;
        }
        {
            LagProbe::Scope const s("eb.maybeAutosave");
            LevelProfile::get().maybeAutosave();
        }
    }

    this->clearBuffer();
    this->m_pendingRelease = false;
    {
        LagProbe::Scope const s("eb.saveCounterClear");
        SaveFrameCounter::get().clear();
    }
    {
        LagProbe::Scope const s("eb.syncButtons");
        this->syncButtonState(playLayer);
    }
}

void AssistController::flushPendingRelease(PlayLayer* playLayer) {
    if (!this->m_pendingRelease || !playLayer) return;
    if (this->m_attemptTick < this->m_pendingReleaseTick) return;

    bool const p1 = this->m_pendingReleaseP1;
    bool const holding = this->userHolding(p1);

    if (this->m_pendingReleaseHold) {
        auto* p = p1 ? playLayer->m_player1 : playLayer->m_player2;
        if (p && p->m_isDashing
            && this->m_attemptTick < this->m_pendingReleaseTick + 90) {
            return;
        }
    }

    // User already released inside the safe window — nothing to inject.
    if (!holding) {
        this->m_pendingRelease = false;
        this->m_pendingReleaseHold = false;
        this->m_pendingReleaseContinuous = false;
        return;
    }

    // Continuous: allow hold until releaseLatest; tap modes: release at preferred.
    if (this->m_pendingReleaseContinuous
        && this->m_attemptTick < this->m_pendingReleaseLatest) {
        return;
    }

    this->m_pendingRelease = false;
    this->m_pendingReleaseHold = false;
    this->m_pendingReleaseContinuous = false;
    this->m_injecting = true;
    playLayer->handleButton(false, JUMP_BUTTON, p1);
    this->m_injecting = false;
    AssistLog::debug(fmt::format(
        "hold-assist: injected release @{} (p{})", this->m_attemptTick, p1 ? 1 : 2
    ));
}

void AssistController::fireBuffered(
    PlayLayer* playLayer, PlayerObject* player, bool allowRecheck
) {
    int const elapsed = this->m_attemptTick - this->m_buffer.pressTick;
    int const remaining = this->m_buffer.limit - elapsed;

    // Orb/window-aware recheck: stay inside [dMin, dMax]; never slide late into death.
    // Skipped for CPS / mode-change force-fire.
    if (allowRecheck
        && player
        && remaining > 0
        && this->m_buffer.rechecks < 2
        && this->m_simDt > 0.f) {
        int const absMin = this->m_buffer.dMin >= 0
            ? this->m_buffer.pressTick + this->m_buffer.dMin
            : this->m_buffer.pressTick;
        int const absMax = this->m_buffer.dMax >= 0
            ? this->m_buffer.pressTick + this->m_buffer.dMax
            : this->m_buffer.pressTick + this->m_buffer.limit;
        int const deadline = this->m_buffer.pressTick + this->m_buffer.limit;
        int const robustK = estimateRobustK(player, this->m_dtVarEma);

        if (this->m_attemptTick < absMin) {
            // Too early for orb window — wait (orb dMin force; not slide-capped).
            int const oldFire = this->m_buffer.fireTick;
            this->m_buffer.fireTick = absMin;
            ++this->m_buffer.rechecks;
            if (StageConfig::debugLogging() || StageConfig::perfTrace()) {
                auto const detail = fmt::format(
                    "recheck: oldFire={}→newFire={} (orb-dMin wait tick={})",
                    oldFire, absMin, this->m_attemptTick
                );
                AssistLog::debug(detail);
                PerfTrace::get().note("recheck", detail);
            }
            return;
        }
        if (this->m_attemptTick > absMax && this->m_buffer.requireOrb) {
            // Past window — fire immediately
        }
        else if (!this->m_buffer.requireOrb || this->m_attemptTick <= absMax) {
            PlanOptions opts;
            opts.budgetUs = StageConfig::simBudgetUs();
            opts.tickBudget = StageConfig::tickBudgetPerPress() / 4;
            opts.capMul = StageConfig::autoTune()
                ? LevelProfile::get().capMul(this->m_buffer.mode)
                : 1.f;
            opts.maxBranches = 2;
            opts.smartDepth = 1;
            fillPlanRobustLatency(opts, player, this->m_dtVarEma, true);
            auto re = ShadowSimulator::get().plan(
                playLayer, player, this->m_buffer.down, remaining,
                this->m_simDt, this->m_simDt, opts
            );

            bool const planOk =
                re.action == PlanAction::Delay
                && re.delay > 0
                && !re.budgetHit
                && (re.threatCleared || re.requireOrb || this->m_buffer.requireOrb);

            if (planOk) {
                int const jumpRel = re.jumpTick > 0 ? re.jumpTick : re.delay;
                int proposed = resolveFireTick(
                    this->m_attemptTick, re.delay, jumpRel, this->m_buffer.requireOrb
                );
                if (this->m_buffer.dMin >= 0) {
                    proposed = std::max(proposed, absMin);
                }
                if (this->m_buffer.dMax >= 0) {
                    proposed = std::min(proposed, absMax);
                }
                proposed = std::min(proposed, deadline);

                int const oldFire = this->m_buffer.fireTick;
                int const accepted = acceptRecheckSlide(
                    oldFire,
                    this->m_attemptTick,
                    proposed,
                    absMax,
                    robustK,
                    deadline,
                    this->m_buffer.requireOrb,
                    true
                );
                if (accepted > 0) {
                    ++this->m_buffer.rechecks;
                    this->m_buffer.fireTick = accepted;
                    this->m_buffer.expectedJumpTick = this->m_attemptTick + jumpRel;
                    this->m_buffer.planDelay = re.delay;
                    if (StageConfig::debugLogging() || StageConfig::perfTrace()) {
                        auto const detail = fmt::format(
                            "recheck: oldFire={}→newFire={} jump={} Δ={} (tick={})",
                            oldFire,
                            accepted,
                            this->m_buffer.expectedJumpTick,
                            accepted - oldFire,
                            this->m_attemptTick
                        );
                        AssistLog::debug(detail);
                        PerfTrace::get().note("recheck", detail);
                    }
                    return;
                }
            }
        }
    }

    bool const wasPress = this->m_buffer.down;
    bool const player1 = this->m_buffer.player1;
    bool const requireHold = this->m_buffer.requireHold;
    bool const continuous = this->m_buffer.continuous;
    bool const letGo = this->m_buffer.releaseSeen || !this->userHolding(player1);
    int const delayed = std::max(0, this->m_attemptTick - this->m_buffer.pressTick);
    int const planId = this->m_buffer.planId;
    bool const expectOrb = this->m_buffer.requireOrb;
    int releaseAfter = this->m_buffer.releaseAfter;
    int releaseLatest = this->m_buffer.releaseLatest;
    if (releaseAfter < 0) releaseAfter = requireHold ? 40 : 1;
    if (releaseLatest < releaseAfter) releaseLatest = releaseAfter;

    this->inject(playLayer, this->m_buffer.down);
    AssistLog::printSavedPress(
        delayed, this->m_buffer.pressTick, this->m_attemptTick,
        this->m_buffer.limit, this->estimatedTps()
    );
    if (delayed > 0
        && (StageConfig::saveFeedbackSound()
            || StageConfig::saveFeedbackIcon()
            || StageConfig::saveFeedbackCounter())) {
        SaveFeedback::get().onSave(playLayer, player, delayed);
    }
    if (!wasPress) {
        AssistLog::debug(fmt::format("(assisted RELEASE, tick={})", this->m_attemptTick));
    }

    this->m_outcomePending = true;
    this->m_outcomePlanId = planId;
    this->m_outcomeUntil = this->m_attemptTick + 96;
    this->m_outcomePlayer1 = player1;
    this->m_outcomeExpectOrb = expectOrb;
    this->m_outcomePlanDelay = delayed > 0;
    this->m_outcomeStartX = player ? player->getPositionX() : 0.f;

    this->clearBuffer();

    // After assisted press: schedule safe release (preferred / force-at-latest).
    if (wasPress) {
        this->m_pendingRelease = true;
        this->m_pendingReleaseP1 = player1;
        this->m_pendingReleaseHold = requireHold;
        this->m_pendingReleaseContinuous = continuous;
        if (letGo) {
            // User already up — release next tick (buffer swallowed their release).
            this->m_pendingReleaseTick = this->m_attemptTick + 1;
            this->m_pendingReleaseLatest = this->m_attemptTick + 1;
        }
        else {
            this->m_pendingReleaseTick = this->m_attemptTick + releaseAfter;
            this->m_pendingReleaseLatest = this->m_attemptTick + releaseLatest;
            AssistLog::debug(fmt::format(
                "hold-assist: arm release [{},{}] @fire={} cont={}",
                this->m_pendingReleaseTick,
                this->m_pendingReleaseLatest,
                this->m_attemptTick,
                continuous ? 1 : 0
            ));
        }
    }
}

void AssistController::onPhysicsTick(PlayLayer* playLayer, float dt) {
    PerfTrace::get().beginTick(this->m_attemptTick, dt);
    // endTick runs on every exit path (including early returns).
    struct TickEndGuard {
        ~TickEndGuard() { PerfTrace::get().endTick(); }
    } tickEndGuard;

    if (dt > 0.f) {
        if (this->m_attemptTick < 5 || dt > 0.6f * this->m_lastDt) this->m_lastDt = dt;
        float tw = playLayer ? playLayer->m_gameState.m_timeWarp : 1.f;
        if (tw <= 0.f) tw = 1.f;
        this->m_simDt = this->m_lastDt / tw;
        float const err = dt - this->m_dtEma;
        this->m_dtEma += 0.1f * err;
        float const denom = std::max(this->m_dtEma, 1e-4f);
        this->m_dtVarEma = 0.1f * std::abs(err) / denom + 0.9f * this->m_dtVarEma;
    }
    ++this->m_attemptTick;
    if (!playLayer) return;

    {
        PerfTrace::Scope const scope("pollOutcome");
        this->pollOutcome(playLayer);
    }
    {
        PerfTrace::Scope const scope("flushPendingRelease");
        this->flushPendingRelease(playLayer);
    }
    {
        PerfTrace::Scope const scope("saveFeedbackTick");
        SaveFeedback::get().onPhysicsTick();
    }
    {
        PerfTrace::Scope const scope("selfBakeIdle");
        SelfBakeRecorder::get().onPhysicsTick(playLayer);
    }

    auto* p1 = playLayer->m_player1;
    if (p1) {
        this->m_lastPlayerX = p1->getPositionX();
        this->m_lastMode = StageConfig::kindOf(p1);
        if (!this->m_startSnapshotTaken && this->m_attemptTick <= 2) {
            PerfTrace::Scope const scope("captureSnapshot");
            this->m_startSnapshotSlot = ShadowSimulator::get().captureSnapshot(p1);
            this->m_startSnapshotTaken = this->m_startSnapshotSlot >= 0;
        }

        bool const modeChanged = this->m_modeKnown && (
            this->m_modeShip != p1->m_isShip ||
            this->m_modeBall != p1->m_isBall ||
            this->m_modeBird != p1->m_isBird ||
            this->m_modeDart != p1->m_isDart ||
            this->m_modeRobot != p1->m_isRobot ||
            this->m_modeSpider != p1->m_isSpider ||
            this->m_modeSwing != p1->m_isSwing
        );
        this->m_modeShip = p1->m_isShip;
        this->m_modeBall = p1->m_isBall;
        this->m_modeBird = p1->m_isBird;
        this->m_modeDart = p1->m_isDart;
        this->m_modeRobot = p1->m_isRobot;
        this->m_modeSpider = p1->m_isSpider;
        this->m_modeSwing = p1->m_isSwing;
        this->m_modeKnown = true;

        if (modeChanged && this->m_buffer.state != BufferState::Idle) {
            AssistLog::debug("mode change → delivering buffered input");
            PerfTrace::Scope const scope("fireBuffered.modeChange");
            this->fireBuffered(playLayer, p1, false);
            this->updateOverlay();
        }

        {
            PerfTrace::Scope const scope("tickLearning");
            this->tickLearning(playLayer, p1);
        }
    }

    {
        PerfTrace::Scope const scope("trajectory");
        TrajectoryVisualizer::get().onPhysicsTick(playLayer, dt);
    }

    this->pruneClickTimes();
    int const cpsLim = StageConfig::cpsLimit();
    if (cpsLim > 0) {
        bool const limited = this->currentCps() >= cpsLim;
        if (this->m_cpsLocked && !limited) {
            this->m_cpsLocked = false;
            AssistLog::info(fmt::format("CPS: unlocked (now {}/{})", this->currentCps(), cpsLim));
        }
        if (limited && this->m_buffer.state == BufferState::Buffered && p1) {
            PerfTrace::Scope const scope("fireBuffered.cps");
            this->fireBuffered(playLayer, p1, false);
        }
    }

    if (this->m_buffer.state == BufferState::Idle) {
        this->updateOverlay();
        return;
    }

    auto* player = this->m_buffer.player1 ? playLayer->m_player1 : playLayer->m_player2;
    if (!player || player->m_isDead) {
        this->clearBuffer();
        this->syncButtonState(playLayer);
        this->updateOverlay();
        return;
    }

    if (this->m_buffer.state == BufferState::Buffered) {
        bool const due = shouldFireBuffered(
            this->m_attemptTick,
            this->m_buffer.fireTick,
            this->m_buffer.pressTick,
            this->m_buffer.planDelay,
            this->m_buffer.limit
        );
        if (due) {
            PerfTrace::Scope const scope("fireBuffered");
            this->fireBuffered(playLayer, player);
        }
    }
    else if (this->m_buffer.state != BufferState::Idle) {
        this->clearBuffer();
    }
    this->updateOverlay();
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
    this->m_buffer = {};
    this->m_buffer.state = BufferState::Buffered;
    this->m_buffer.down = down;
    this->m_buffer.player1 = player1;
    this->m_buffer.pressTick = effectTick;
    this->m_buffer.planDelay = delay;
    this->m_buffer.limit = std::max(limit, delay);
    // Bake has no sim jumpTick — fire at planned delay.
    this->m_buffer.fireTick = resolveFireTick(effectTick, delay, delay, false);
    this->m_buffer.expectedJumpTick = this->m_buffer.fireTick;
    this->m_buffer.decisionX = player->getPositionX();
    this->m_buffer.mode = kind;

    // G4 bake heuristic (no file format change): if Orb Assist would have intent
    // near this bake delay, apply the same [dMin,dMax] recheck/requireOrb rules.
    if (down && StageConfig::assistOrbs()) {
        int const dMin = std::max(0, earliestTick - effectTick);
        int const dMax = std::max(dMin, latestTick - effectTick);
        bool const nearOrb = ShadowSimulator::spatialOrbHint(
            playLayer, player, std::max(limit, dMax + 2), this->m_simDt
        );
        if (nearOrb) {
            this->m_buffer.requireOrb = true;
            this->m_buffer.dMin = dMin;
            this->m_buffer.dMax = dMax;
            this->m_buffer.fireTick = resolveFireTick(effectTick, delay, delay, true);
            this->m_buffer.fireTick = std::max(this->m_buffer.fireTick, effectTick + dMin);
            this->m_buffer.fireTick = std::min(this->m_buffer.fireTick, effectTick + dMax);
            AssistLog::debug(fmt::format(
                "bake orb-heuristic win=[{},{}] ticks", dMin, dMax
            ));
        }
    }
    this->m_buffer.fireTick = std::min(
        this->m_buffer.fireTick, this->m_buffer.pressTick + this->m_buffer.limit
    );

    AssistLog::debug(fmt::format(
        "bake ASSIST {} +{} ticks (window [{},{}] fire={})",
        down ? "press" : "release", delay, win->earliest, win->latest,
        this->m_buffer.fireTick
    ));
    this->updateOverlay();
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

    // Live/Hybrid-live: sim mismatch ring → force Pass (no explore under UNTRUST).
    if (DivergenceTracker::get().simUntrusted()) {
        AssistLog::debug("UNTRUST pass (press)");
        m_untrustPassHint = true;
        SimPlan dummy;
        dummy.action = PlanAction::Pass;
        dummy.firstDeathTick = -1;
        dummy.continuous = player->m_isDart || player->m_isShip || player->m_isSwing;
        recordDecision(playLayer, player, true, dummy, effectTick, 0, limit);
        updateOverlay();
        return true;
    }
    m_untrustPassHint = false;

    // Dense / rhythmic spam (esp. wave corridors): Pass unless orb/pad or bake nearby.
    {
        bool const nearOrb = nearOrbOrPadHint(playLayer, player, limit, this->m_simDt);
        bool const nearBake = nearBakeWindowHint(
            player, player1, true, effectTick, this->m_simDt
        );
        if (ClickIntentGuard::get().shouldPassThrough(
                slot, effectTick, kind, nearOrb, nearBake
            )) {
            AssistLog::debug(fmt::format(
                "intent: spam Pass (mode={}, tick={})", StageConfig::kindName(kind), effectTick
            ));
            SimPlan dummy;
            dummy.action = PlanAction::Pass;
            dummy.firstDeathTick = -1;
            dummy.continuous = player->m_isDart || player->m_isShip || player->m_isSwing;
            this->recordDecision(playLayer, player, true, dummy, effectTick, 0, limit);
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
    // Soft warn: shorten look-ahead slightly (no hard Pass).
    if (DivergenceTracker::get().shouldWarn()) {
        opts.capMul *= 0.85f;
    }
    opts.maxBranches = 4;
    opts.smartDepth = StageConfig::smartDepth();
    opts.smartCombos = StageConfig::smartCombos();
    opts.smartHorizon = StageConfig::smartHorizon();
    opts.attemptStartTick = effectTick;
    // Density without orb/bake override — softens Wave λ / bumps auto k when
    // we still assist (e.g. orb in spam corridor).
    bool const spamDense = ClickIntentGuard::get().shouldPassThrough(
        slot, effectTick, kind
    );
    fillPlanRobustLatency(opts, player, this->m_dtVarEma, spamDense);
    fillBakePreferBand(
        opts, player, player1, true, effectTick, limit, this->m_attemptTick, this->m_simDt
    );

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
        // Vanilla/Pass click timing — feed earliness histogram (never inject saves).
        if (StageConfig::autoTune() && profile.loaded()) {
            profile.noteEarliness(kind, x, 0, "pass");
        }
        return true;
    }

    int delay = plan.delay;
    if (StageConfig::autoTune() && profile.loaded()) {
        if (auto const* spot = profile.trySpot(kind, x)) {
            int off = profile.preferredDelayOffset(kind, x);
            bool const bakePrefer =
                opts.preferDelayMin >= 0
                && opts.preferDelayMax >= opts.preferDelayMin;
            // Density for explore gate even when orb forced us past spam Pass.
            bool const spamDense = ClickIntentGuard::get().shouldPassThrough(
                slot, effectTick, kind
            );
            if (AssistLearning::allowHistogramExplore(
                    kind,
                    spot->histN,
                    spamDense,
                    bakePrefer,
                    spot->diedAfterAssist
                )) {
                off += (std::rand() % 3) - 1;
                AssistLog::debug(fmt::format(
                    "learn: hist explore ±1 @{} histN={}",
                    LevelProfile::spotBucket(x), spot->histN
                ));
            }
            delay = delay + off;
        }
        // Clamp bias into activation window when requireOrb
        if (plan.requireOrb && plan.dMin >= 0 && plan.dMax >= plan.dMin) {
            delay = std::max(plan.dMin, std::min(plan.dMax, delay));
        }
        delay = std::max(1, std::min(limit, delay));
    }

    this->m_buffer = {};
    this->m_buffer.state = BufferState::Buffered;
    this->m_buffer.down = true;
    this->m_buffer.player1 = player1;
    this->m_buffer.pressTick = effectTick;
    this->m_buffer.planDelay = delay;
    this->m_buffer.limit = limit;
    int const jumpRel = plan.jumpTick > 0 ? plan.jumpTick : delay;
    this->m_buffer.expectedJumpTick = effectTick + jumpRel;
    this->m_buffer.fireTick = resolveFireTick(
        effectTick, delay, jumpRel, plan.requireOrb
    );
    this->m_buffer.continuous = plan.continuous;
    this->m_buffer.decisionX = x;
    this->m_buffer.mode = kind;
    this->m_buffer.requireOrb = plan.requireOrb;
    this->m_buffer.requireHold = plan.requireHold;
    this->m_buffer.dMin = plan.dMin;
    this->m_buffer.dMax = plan.dMax;
    this->m_buffer.releaseAfter = plan.releaseAfter;
    this->m_buffer.releaseEarliest = plan.releaseEarliest;
    this->m_buffer.releaseLatest = plan.releaseLatest;
    this->m_buffer.planId = plan.planId;

    // Keep fire inside activation window
    if (plan.dMin >= 0) {
        this->m_buffer.fireTick = std::max(this->m_buffer.fireTick, effectTick + plan.dMin);
    }
    if (plan.dMax >= 0) {
        this->m_buffer.fireTick = std::min(this->m_buffer.fireTick, effectTick + plan.dMax);
    }
    // Never arm past the forgiveness deadline.
    this->m_buffer.fireTick = std::min(this->m_buffer.fireTick, effectTick + limit);

    AssistLog::debug(fmt::format(
        "ASSIST press +{} ticks id={} ring={} hold={} rel=[{},{}]@{} win=[{},{}] fire={} jump={}",
        delay, plan.planId, plan.activatedRing, plan.requireHold,
        plan.releaseEarliest, plan.releaseLatest, plan.releaseAfter,
        plan.dMin, plan.dMax,
        this->m_buffer.fireTick, this->m_buffer.expectedJumpTick
    ));
    this->recordDecision(playLayer, player, true, plan, effectTick, delay, limit);
    this->updateOverlay();
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

    if (DivergenceTracker::get().simUntrusted()) {
        AssistLog::debug("UNTRUST pass (release)");
        m_untrustPassHint = true;
        updateOverlay();
        return true;
    }

    // Continuous spam: do not delay releases either (would steal hold state).
    bool const continuous =
        player->m_isDart || player->m_isShip || player->m_isSwing;
    if (continuous) {
        bool const nearOrb = nearOrbOrPadHint(playLayer, player, limit, this->m_simDt);
        bool const nearBake = nearBakeWindowHint(
            player, player1, false, effectTick, this->m_simDt
        );
        if (ClickIntentGuard::get().shouldPassThrough(
                slot, effectTick, kind, nearOrb, nearBake
            )) {
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
    if (DivergenceTracker::get().shouldWarn()) {
        opts.capMul *= 0.85f;
    }
    opts.maxBranches = 4;
    opts.attemptStartTick = effectTick;
    bool const spamDenseRel = ClickIntentGuard::get().shouldPassThrough(
        slot, effectTick, kind
    );
    fillPlanRobustLatency(opts, player, this->m_dtVarEma, spamDenseRel);
    fillBakePreferBand(
        opts, player, player1, false, effectTick, limit, this->m_attemptTick, this->m_simDt
    );

    auto plan = ShadowSimulator::get().plan(playLayer, player, false, limit, m_simDt, firstDt, opts);

    if (plan.action != PlanAction::Delay) {
        recordDecision(playLayer, player, false, plan, effectTick, 0, limit);
        return true;
    }

    int delay = plan.delay;
    if (StageConfig::autoTune() && profile.loaded()) {
        if (auto const* spot = profile.trySpot(kind, x)) {
            int off = profile.preferredDelayOffset(kind, x);
            bool const bakePrefer =
                opts.preferDelayMin >= 0
                && opts.preferDelayMax >= opts.preferDelayMin;
            bool const spamDense = ClickIntentGuard::get().shouldPassThrough(
                slot, effectTick, kind
            );
            if (AssistLearning::allowHistogramExplore(
                    kind,
                    spot->histN,
                    spamDense,
                    bakePrefer,
                    spot->diedAfterAssist
                )) {
                off += (std::rand() % 3) - 1;
                AssistLog::debug(fmt::format(
                    "learn: hist explore ±1 release @{} histN={}",
                    LevelProfile::spotBucket(x), spot->histN
                ));
            }
            delay = delay + off;
        }
        delay = std::max(1, std::min(limit, delay));
    }

    this->m_buffer = {};
    this->m_buffer.state = BufferState::Buffered;
    this->m_buffer.down = false;
    this->m_buffer.player1 = player1;
    this->m_buffer.pressTick = effectTick;
    this->m_buffer.planDelay = delay;
    this->m_buffer.limit = limit;
    // Releases have no jumpTick — fire at delay.
    this->m_buffer.fireTick = resolveFireTick(effectTick, delay, delay, false);
    this->m_buffer.expectedJumpTick = this->m_buffer.fireTick;
    this->m_buffer.continuous = plan.continuous;
    this->m_buffer.decisionX = x;
    this->m_buffer.mode = kind;
    this->m_buffer.fireTick = std::min(this->m_buffer.fireTick, effectTick + limit);

    AssistLog::debug(fmt::format(
        "ASSIST release +{} ticks (now dies @+{}, limit {}, tick={} fire={})",
        delay, plan.firstDeathTick, limit, effectTick, this->m_buffer.fireTick
    ));
    this->recordDecision(playLayer, player, false, plan, effectTick, delay, limit);
    this->updateOverlay();
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

    this->m_userHolding[player1 ? 0 : 1] = down;

    // Second click while a hold-assist release is armed: cancel it and re-plan.
    if (down
        && this->m_pendingRelease
        && this->m_pendingReleaseP1 == player1) {
        this->m_pendingRelease = false;
        this->m_pendingReleaseHold = false;
        this->m_pendingReleaseContinuous = false;
        AssistLog::debug("hold-assist: cancel pending release (re-click)");
    }

    // Attribution: see resolveEffectTick / assist_reference.md "effectTick".
    // m_inRealUpdate ↔ inside PlayerObject::update (CBF mid-step).
    bool const midStep = this->m_inRealUpdate;
    bool const continuous = continuousModePlayer(player);
    int const effectTick = resolveEffectTick(this->m_attemptTick, midStep, continuous);

    auto finish = [&](bool passThrough) -> bool {
        if (passThrough) {
            SelfBakeRecorder::get().noteVanillaInput(
                playLayer, player, player1, down, effectTick, this->m_simDt
            );
        }
        return passThrough;
    };

    // Min click interval: skip assist only — never block the vanilla click.
    if (down) {
        int const minMs = StageConfig::minClickIntervalMs();
        if (minMs > 0 && !this->m_clickTimes.empty()) {
            auto const elapsed = std::chrono::steady_clock::now() - this->m_clickTimes.back();
            if (elapsed < std::chrono::milliseconds(minMs)) {
                AssistLog::debug(fmt::format(
                    "click debounce: no assist ({}ms < {}ms), pass through",
                    std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
                    minMs
                ));
                return finish(true);
            }
        }
        this->noteUserClick();
        ClickIntentGuard::get().notePress(
            player1 ? 0 : 1, effectTick, StageConfig::kindOf(player)
        );
    }

    // firstDt: full sim dt between frames; CBF remainder / timeWarp when mid-step.
    // Never fold the remainder into effectTick (that caused ±1 vs between-frames).
    float firstDt = this->m_simDt;
    if (midStep) {
        float rem = this->m_lastDt - this->m_subDtSeen;
        float const minDt = this->m_lastDt * 0.02f;
        if (rem < minDt) rem = minDt;
        if (rem > this->m_lastDt) rem = this->m_lastDt;
        float tw = playLayer->m_gameState.m_timeWarp;
        if (tw <= 0.f) tw = 1.f;
        firstDt = rem / tw;
    }

    // Attribution log only under debug-logging (not every press into perf file).
    if (down && StageConfig::debugLogging()) {
        AssistLog::debug(fmt::format(
            "attrib: effectTick={} attemptTick={} midStep={} continuous={} firstDt={:.6f} simDt={:.6f} p{}",
            effectTick,
            this->m_attemptTick,
            midStep ? 1 : 0,
            continuous ? 1 : 0,
            firstDt,
            this->m_simDt,
            player1 ? 1 : 2
        ));
    }

    // Track real (non-swallowed) inputs for learning timelines.
    // Buffered/swallowed ones are recorded when we inject or as releaseSeen.
    bool const ours =
        this->m_buffer.state != BufferState::Idle && this->m_buffer.player1 == player1;

    if (!down) {
        if (ours) {
            if (!this->m_buffer.down) {
                return false;
            }
            this->m_buffer.releaseSeen = true;
            this->m_buffer.releaseTick = effectTick;
            this->m_log.noteInput(effectTick, false);
            return false;
        }

        if (!this->shouldAssistPlayer(player) || !StageConfig::assistRelease()) {
            return finish(true);
        }
        if (this->m_attemptTick <= 2) return finish(true);
        bool const pass = this->planRelease(playLayer, player, player1, effectTick, firstDt);
        if (pass) this->m_log.noteInput(effectTick, false);
        return finish(pass);
    }

    if (ours && this->m_buffer.state == BufferState::Buffered && this->m_buffer.down) {
        if (!this->m_buffer.releaseSeen) {
            return false; // same press still armed; swallow duplicate downs
        }
        // Press B before fireTick after Release A: fire A first, then plan B.
        this->fireBuffered(playLayer, player);
    }
    if (ours && this->m_buffer.state == BufferState::Buffered && !this->m_buffer.down) {
        this->fireBuffered(playLayer, player);
    }

    if (!this->shouldAssistPlayer(player) || !StageConfig::assistPress()) {
        return finish(true);
    }
    if (this->m_attemptTick <= 2) return finish(true);
    bool const pass = this->planPress(playLayer, player, player1, effectTick, firstDt);
    if (pass) this->m_log.noteInput(effectTick, true);
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


