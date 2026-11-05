#include "AssistController.hpp"
#include "AssistClock.hpp"
#include "AssistLearning.hpp"
#include "AssistPerfStats.hpp"
#include "AssistSpikeWatch.hpp"
#include "TravelForesightCache.hpp"
#include "AssistScopeGate.hpp"
#include "BakeMap.hpp"
#include "BakeVerifier.hpp"
#include "ClickIntentGuard.hpp"
#include "ModeAssist.hpp"
#include "ShiftedInputQueue.hpp"
#include "SpamAssistPolicy.hpp"
#include "HybridVerifyCache.hpp"
#include "MissAssistContracts.hpp"
#include "SelfBakeRecorder.hpp"
#include "DivergenceTracker.hpp"
#include "AssistRealMetrics.hpp"
#include "AssistSimEstimate.hpp"
#include "LevelCacheIndex.hpp"
#include "LevelFingerprint.hpp"
#include "GhostParityProbe.hpp"
#include "SimTrust.hpp"
#include "PerfectAssistPolicy.hpp"
#include "OrbAlign.hpp"
#include "SpotTrustMap.hpp"
#include "LevelProfile.hpp"
#include "ShadowSimulator.hpp"
#include "TravelForesight.hpp"
#include "TriggerWorldBaker.hpp"
#include "TriggerWorldStore.hpp"
#include <chrono>
#include <cstring>
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"
#include "../debug/LagProbe.hpp"
#include "../debug/PerfTrace.hpp"
#include "../dev/DevClockContract.hpp"
#include "../dev/DevConfig.hpp"
#include "../dev/DevCycleRecorder.hpp"
#include "../dev/DevReport.hpp"
#include "../ui/DevOverlay.hpp"
#include "../ui/SaveFeedback.hpp"
#include "../ui/SaveFrameCounter.hpp"
#include "../ui/StatusOverlay.hpp"
#include "../ui/BakePathVisualizer.hpp"
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
        auto const kind = StageConfig::kindOf(player);
        auto const params = StageConfig::paramsFor(player);
        opts.latencyWeight = params.latencyWeight;
        // Wave λ prefers earliest hard — spam+orb assists phase-shift into spikes.
        // Soften only on spam-dense assists (Pass-excluded paths). Parity: ModeAssist.
        if (spamDense
            && ModeAssist::forKind(kind).kind() == GameModeKind::Wave
            && opts.latencyWeight > 0.5f) {
            opts.latencyWeight *= 0.65f;
        }
        opts.modeSnap = ModeAssist::snapshotFor(kind, params.ticks, true);
        opts.modeSnapValid = true;
    }
}

// Map a nearby BakeMap window onto delay ticks for Live prefer-band ordering.
// Absolute bake frames → live ticks (unmapTick), then delays relative to effectTick.
static void fillBakePreferBandFromReuse(
    PlanOptions& opts,
    HybridPressReuse const& reuse,
    int effectTick,
    int limit,
    float simDt
) {
    if (!reuse.hasWindow || limit <= 0 || simDt <= 0.f) return;
    double const tps = reuse.bakeTps > 0.0 ? reuse.bakeTps : AssistClock::kNominalTps;
    auto& clock = AssistClock::get();
    int eAbs = clock.unmapTick(tps, reuse.earliest, simDt);
    int lAbs = clock.unmapTick(tps, reuse.latest, simDt);
    int nAbs = clock.unmapTick(tps, reuse.nominalFrame, simDt);
    if (lAbs < eAbs) std::swap(lAbs, eAbs);
    // Prefer band toward nominal when still before it.
    int e = std::max(0, eAbs - effectTick);
    int l = std::max(0, lAbs - effectTick);
    int n = std::max(0, nAbs - effectTick);
    if (n >= e && n <= l) {
        // Tighten prefer toward nominal..latest when inside the window ahead.
        e = n;
    }
    e = std::max(0, std::min(limit, e));
    l = std::max(0, std::min(limit, l));
    if (e > l) return;
    opts.preferDelayMin = e;
    opts.preferDelayMax = l;
}

static void fillBakePreferBand(
    PlanOptions& opts,
    PlayerObject* player,
    bool player1,
    bool down,
    int effectTick,
    int limit,
    int attemptTick,
    float simDt,
    HybridPressReuse const* reuse = nullptr
) {
    if (reuse && reuse->hasWindow) {
        fillBakePreferBandFromReuse(opts, *reuse, effectTick, limit, simDt);
        return;
    }
    if (!player || limit <= 0 || simDt <= 0.f) return;
    auto const& bake = BakeMap::get();
    if (!bake.loaded() || bake.data().clicks.empty()) return;

    double tps = bake.data().tps > 0.0 ? bake.data().tps : AssistClock::kNominalTps;
    int const approxFrame = AssistClock::get().remapTick(tps, attemptTick, simDt);
    auto const* win = bake.findNear(
        player->getPositionX(), approxFrame, down, !player1,
        StageConfig::kindOf(player)
);
    if (!win) return;

    HybridPressReuse tmp;
    tmp.hasWindow = true;
    tmp.earliest = win->earliest;
    tmp.latest = win->latest;
    tmp.nominalFrame = win->nominalFrame;
    tmp.bakeTps = tps;
    fillBakePreferBandFromReuse(opts, tmp, effectTick, limit, simDt);
}

// Nearby bake click → ClickIntentGuard must not Pass (assist allowed).
// Hybrid callers clear this after bake verify rejects the window (avoid forcing
// full Live on every moderate-CPS click near a stale bake).
static bool nearBakeWindowHint(
    PlayerObject* player,
    bool player1,
    bool down,
    int attemptTick,
    float simDt,
    HybridPressReuse const* reuse = nullptr
) {
    if (reuse) return reuse->hasWindow;
    if (!player || simDt <= 0.f) return false;
    auto const& bake = BakeMap::get();
    if (!bake.loaded() || bake.data().clicks.empty()) return false;
    double tps = bake.data().tps > 0.0 ? bake.data().tps : AssistClock::kNominalTps;
    int const approxFrame = AssistClock::get().remapTick(tps, attemptTick, simDt);
    return bake.findNear(
        player->getPositionX(), approxFrame, down, !player1,
        StageConfig::kindOf(player)
) != nullptr;
}

// Hybrid Live fallthrough: never HUS-inject before bake nominal (press or release).
// Returns true when caller should Pass instead of armHoldUntilSafe.
static bool husBlockedBeforeBakeNominal(
    HybridPressReuse const& reuse,
    int effectTick,
    float simDt,
    int& outNomTick
) {
    outNomTick = -1;
    if (!reuse.hasWindow || simDt <= 0.f) return false;
    double const bakeTps = reuse.bakeTps > 0.0
        ? reuse.bakeTps
        : AssistClock::kNominalTps;
    outNomTick = AssistClock::get().unmapTick(
        bakeTps, reuse.nominalFrame, simDt
);
    return effectTick < outNomTick;
}

static bool nearOrbOrPadHint(
    PlayLayer* playLayer, PlayerObject* player, int limit, float simDt
) {
    if (!StageConfig::assistOrbs()) return false;
    if (!playLayer || !player || limit <= 0 || simDt <= 0.f) return false;
    // Same TravelForesight cone as ShadowSimulator::spatialOrbHint (orbs + pads).
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
    return AssistClock::get().simTpsFromDt(this->m_simDt);
}

double AssistController::wallTps() const {
    return AssistClock::get().wallTpsFromDt(this->m_lastDt);
}

std::string AssistController::overlayText() const { return m_overlayText; }

void AssistController::clearBuffer(bool keepShiftQueue) {
    this->m_buffer = {};
    if (!keepShiftQueue) {
        this->m_shiftQueue.clear();
        this->m_shiftDrainPass = false;
    }
}

void AssistController::beginShiftQueue(int shiftTicks) {
    this->m_shiftDrainPass = false;
    this->m_shiftQueue.beginShift(shiftTicks);
}

void AssistController::tickShiftQueue(PlayLayer* playLayer) {
    if (!playLayer) return;
    ShiftedInput next{};
    bool popped = false;
    if (this->m_shiftDrainPass) {
        popped = this->m_shiftQueue.tryDrainOne(next);
        if (this->m_shiftQueue.empty()) {
            this->m_shiftDrainPass = false;
        }
    }
    else {
        popped = this->m_shiftQueue.tryPopDue(this->m_attemptTick, next);
    }
    if (!popped) return;

    auto* player = next.player1 ? playLayer->m_player1 : playLayer->m_player2;
    if (!player || player->m_isDead) {
        this->m_shiftQueue.clear();
        this->m_shiftDrainPass = false;
        return;
    }
    AssistLog::debug(fmt::format(
        "shift-queue: inject {} p{} press={} fire={} left={}",
        next.down ? "DOWN" : "UP",
        next.player1 ? 1 : 2,
        next.pressTick,
        next.fireTick,
        this->m_shiftQueue.size()
));
    this->m_assistTouchedRun = true;
    if (next.down) {
        ++this->m_savesAttempt;
        ++this->m_savesLevel;
    }
    this->m_injecting = true;
    playLayer->handleButton(next.down, JUMP_BUTTON, next.player1);
    this->m_injecting = false;
}

void AssistController::onCheckpointLoad() {
    // Do not flush/refine here — idle drain handles the buffer after respawn.
    // Trigger world: keep store + in-flight baker; only reset abs attempt tick.
    SelfBakeRecorder::get().flushSegment(PlayLayer::get(), "checkpoint", false);
    clearBuffer();
    this->m_shiftQueue.clear();
    this->m_shiftDrainPass = false;
    this->m_pendingRelease = false;
    this->m_pendingReleaseHold = false;
    this->m_pendingReleaseContinuous = false;
    this->m_pendingReleaseArmTick = 0;
    this->m_pendingReleaseUserUpTick = -1;
    this->m_pendingReleaseMaxExtend = 0;
    this->m_pendingReleaseTick = 0;
    m_outcomePending = false;
    this->m_attemptTick = 0;
    this->m_startSnapshotTaken = false;
    this->m_startSnapshotSlot = -1;
    ClickIntentGuard::get().reset();
    DevCycleRecorder::get().noteCheckpoint(0);
    LagProbe::get().mark(
        "triggerWorld.checkpoint",
        "keep store / no rebuild / attemptTick=0"
);
}

void AssistController::onLevelComplete(PlayLayer* playLayer) {
    DevCycleRecorder::get().sealAttemptWithOutcomes(
        "complete", true, this->m_attemptTick
);
    SelfBakeRecorder::get().flushMergeOnly(playLayer, "complete");
    LevelProfile::get().save();
    BakeMap::get().save();
    {
        LagProbe::Scope const s("eb.triggerBake.save");
        TriggerWorldStore::get().save();
    }
}

void AssistController::syncButtonState(PlayLayer* playLayer) {
    if (!playLayer) return;
    PlayerObject* players[2] = { playLayer->m_player1, playLayer->m_player2 };
    for (int i = 0; i < 2; ++i) {
        auto* p = players[i];
        if (!p) continue;
        // Extend-hold after user release: Jump stays down until flushPendingRelease.
        if (this->m_pendingRelease
            && this->m_pendingReleaseP1 == (i == 0)
            && !this->m_userHolding[i]) {
            continue;
        }
        auto it = p->m_holdingButtons.find(static_cast<int>(PlayerButton::Jump));
        bool const gdDown = it != p->m_holdingButtons.end() && it->second;
        if (gdDown && !this->m_userHolding[i]) {
            this->m_injecting = true;
            p->releaseButton(PlayerButton::Jump);
            this->m_injecting = false;
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
    // If death already sealed the attempt, open the next; else seal as reset.
    DevCycleRecorder::get().onAttemptReset(0);
    clearBuffer();
    this->m_shiftQueue.clear();
    this->m_shiftDrainPass = false;
    this->m_pendingRelease = false;
    this->m_pendingReleaseHold = false;
    this->m_pendingReleaseContinuous = false;
    this->m_pendingReleaseArmTick = 0;
    this->m_pendingReleaseUserUpTick = -1;
    this->m_pendingReleaseMaxExtend = 0;
    this->m_pendingReleaseTick = 0;
    m_assistTouchedRun = false;
    m_savesAttempt = 0;
    DivergenceTracker::get().clear();
    HybridVerifyCache::get().clearAttempt();
    SimTrust::get().resetAttempt();
    GhostParityProbe::get().resetAttempt();
    TravelForesightCache::get().clear();
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
    m_dafWatch = false;
    m_dafWatchMode = GameModeKind::Cube;
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
    // If ghosts were torn down mid-attempt, re-attach so assist works again.
    if (playLayer && !ShadowSimulator::get().isAttachedTo(playLayer)) {
        AssistLog::debug("resetAttempt: shadow missing — re-attach");
        ShadowSimulator::get().attach(playLayer);
    }
    // Death may have cancelled an in-flight trigger bake — resume if still empty.
    TriggerWorldBaker::get().resumeIfEmpty(playLayer);
}

void AssistController::onLevelInit(PlayLayer* playLayer) {
    m_userHolding[0] = m_userHolding[1] = false;
    SelfBakeRecorder::get().onLevelInit(playLayer);
    resetAttempt(playLayer);
    ShadowSimulator::get().attach(playLayer);
    StatusOverlay::attach(playLayer);
    TrajectoryVisualizer::get().setEnabled(StageConfig::showTrajectory());
    TrajectoryVisualizer::get().attach(playLayer);
    BakePathVisualizer::get().setEnabled(StageConfig::showBakePath());
    BakePathVisualizer::get().attach(playLayer);
    if (playLayer && playLayer->m_level) {
        LevelProfile::get().loadForLevel(playLayer->m_level);
        BakeMap::get().loadForLevel(playLayer->m_level, 0);
        (void)LevelCacheIndex::evictLruIfOverCap();
        TriggerWorldBaker::get().loadOrBuild(playLayer);
        // Movers / TW tracks → dynamic-labelled TrustRate floor (G7b).
        bool const dynamicLabelled = TriggerWorldStore::get().hasTracks();
        AssistRealMetrics::get().setLevelKey(
            LevelProfile::get().key(), dynamicLabelled
);
    }
    updateOverlay();
}

void AssistController::onLevelQuit() {
    // Tear down UI/sim WHILE PlayLayer is still alive. Leaving nodes in
    // objectLayer for setNextScene → ~GameObject / CCObject::release AVs.
    auto* playLayer = PlayLayer::get();
    // Dev traces must flush BEFORE ghost evacuate / detach (partial OK).
    DevCycleRecorder::get().flushQuit();
    ShadowSimulator::get().abortEval();
    TriggerWorldBaker::get().cancel();
    SelfBakeRecorder::get().cancelRefine();
    SelfBakeRecorder::get().flushMergeOnly(playLayer, "quit");
    SelfBakeRecorder::get().onLevelQuit();
    SaveFeedback::get().reset();
    ClickIntentGuard::get().reset();
    // Prefer live detach (removeFromParent) while PlayLayer exists.
    bool const layerAlive = playLayer != nullptr;
    SaveFrameCounter::get().detach(!layerAlive);
    TrajectoryVisualizer::get().detach(!layerAlive);
    BakePathVisualizer::get().detach(!layerAlive);
    StatusOverlay::detach(!layerAlive);

    if (playLayer) {
        ShadowSimulator::get().evacuateGhosts(playLayer);
    }
    // removeFromParentAndCleanup now — never leave ghosts for setNextScene.
    ShadowSimulator::get().detach(false);

    // Persist then clear. Skip PerfTrace sync flush here — large logs were
    // hitching quits; pending lines flush on next enable / natural batch.
    LevelProfile::get().save();
    BakeMap::get().save();
    {
        LagProbe::Scope const s("eb.triggerBake.save");
        TriggerWorldStore::get().save();
    }
    LevelProfile::get().clear();
    TriggerWorldStore::get().clear();
    SimTrust::get().onLevelQuit();
    GhostParityProbe::get().onLevelQuit();
    TravelForesightCache::get().clear();
    this->m_dafWatch = false;
    this->m_dafWatchMode = GameModeKind::Cube;
    // Keep AssistRealMetrics session across levels (corpus report); do not resetSession.
    {
        auto& metrics = AssistRealMetrics::get();
        if (metrics.nOpp() > 0 || metrics.arms() > 0 || metrics.parityProbes() > 0) {
            AssistLog::info(fmt::format(
                "REAL-METRICS {}", metrics.reportLine()
));
            auto const sim = AssistSimEstimate::get().reportLine();
            if (!sim.empty()) {
                AssistLog::info(fmt::format("SIM-ESTIMATE {}", sim));
            }
        }
    }

    // Quiet reset — overlay already detached
    this->clearBuffer();
    this->m_shiftQueue.clear();
    this->m_shiftDrainPass = false;
    this->m_pendingRelease = false;
    this->m_pendingReleaseHold = false;
    this->m_pendingReleaseContinuous = false;
    this->m_pendingReleaseUserUpTick = -1;
    this->m_pendingReleaseMaxExtend = 0;
    this->m_pendingReleaseTick = 0;
    this->m_attemptTick = 0;
    this->m_inRealUpdate = false;
    this->m_subDtSeen = 0.f;
    this->m_modeKnown = false;
    this->m_startSnapshotTaken = false;
    this->m_startSnapshotSlot = -1;
    this->m_lastBakeHint.clear();
    this->m_log.clear();
    this->m_clickTimes.clear();
    this->m_cpsLocked = false;
    this->m_userHolding[0] = this->m_userHolding[1] = false;
}

geode::Result<> AssistController::ensureStartSnapshot(PlayLayer* layer) {
    if (!layer) return geode::Err("No layer");
    // Prefer reserved start slot captured at attempt begin. Only (re)capture
    // while still near frame 0 — never overwrite with mid-run pose at bake time.
    if (!m_startSnapshotTaken || m_startSnapshotSlot < 0) {
        if (layer->m_player1 && m_attemptTick <= 5) {
            m_startSnapshotSlot = ShadowSimulator::get().captureStartSnapshot(layer->m_player1);
            m_startSnapshotTaken = m_startSnapshotSlot >= 0;
        }
    }
    if (!m_startSnapshotTaken
        || m_startSnapshotSlot != ShadowSimulator::START_SNAPSHOT_SLOT) {
        return geode::Err("Reset the level once (start snapshot), then bake");
    }
    if (!ShadowSimulator::get().snapshotSlot(m_startSnapshotSlot)) {
        return geode::Err("Start snapshot missing — reset the level, then bake");
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
    this->m_subDtSeen += AssistClock::gdStepToSeconds(dt);
}

bool AssistController::continuousModeKind(GameModeKind kind) {
    return ModeAssist::forKind(kind).continuous();
}

bool AssistController::continuousModePlayer(PlayerObject* player) {
    if (!player) return false;
    return continuousModeKind(StageConfig::kindOf(player));
}

bool AssistController::releaseAssistModeKind(GameModeKind kind) {
    // Same set as BakePathVisualizer purple windows (not ufo/spider).
    return ModeAssist::forKind(kind).releaseAssistAlways();
}

bool AssistController::releaseAssistModePlayer(PlayerObject* player) {
    if (!player) return false;
    return releaseAssistModeKind(StageConfig::kindOf(player));
}

bool AssistController::shouldPlanRelease(
    PlayLayer* /*playLayer*/,
    PlayerObject* player,
    bool player1,
    int effectTick
) const {
    if (!player) return false;
    if (releaseAssistModePlayer(player)) return true;

    // Discrete (cube/ball/ufo/spider): only dash / hold-orb contexts.
    if (player->m_isDashing) return true;
    if (this->m_pendingReleaseHold) return true;
    if (this->m_buffer.requireHold) return true;

    auto& bake = BakeMap::get();
    if (!bake.loaded() || bake.data().clicks.empty()) return false;

    double tps = bake.data().tps > 0.0 ? bake.data().tps : AssistClock::kNominalTps;
    float const simDt = this->m_simDt > 0.f
        ? this->m_simDt
        : static_cast<float>(1.0 / AssistClock::kNominalTps);
    int const approxFrame = AssistClock::get().remapTick(
        tps, this->m_attemptTick > 0 ? this->m_attemptTick : effectTick, simDt
);
    auto const kind = StageConfig::kindOf(player);
    auto const* win = bake.findNear(
        player->getPositionX(), approxFrame, false, !player1, kind
);
    return win && win->holdAssist;
}

bool AssistController::releaseGateSelfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    expect(releaseAssistModeKind(GameModeKind::Wave), "rel-wave");
    expect(releaseAssistModeKind(GameModeKind::Ship), "rel-ship");
    expect(releaseAssistModeKind(GameModeKind::Swing), "rel-swing");
    expect(releaseAssistModeKind(GameModeKind::Robot), "rel-robot");
    expect(!releaseAssistModeKind(GameModeKind::Cube), "rel-not-cube");
    expect(!releaseAssistModeKind(GameModeKind::Ball), "rel-not-ball");
    expect(!releaseAssistModeKind(GameModeKind::Ufo), "rel-not-ufo");
    expect(!releaseAssistModeKind(GameModeKind::Spider), "rel-not-spider");
    // Viz parity: BakePathVisualizer must match this set.
    expect(
        BakePathVisualizer::releaseWindowMode(GameModeKind::Wave)
            == releaseAssistModeKind(GameModeKind::Wave),
        "viz-wave"
);
    expect(
        BakePathVisualizer::releaseWindowMode(GameModeKind::Cube)
            == releaseAssistModeKind(GameModeKind::Cube),
        "viz-cube"
);
    expect(
        BakePathVisualizer::releaseWindowMode(GameModeKind::Robot)
            == releaseAssistModeKind(GameModeKind::Robot),
        "viz-robot"
);

    if (fails == 0) {
        report = "releaseGateSelfCheck OK";
        return true;
    }
    report = fmt::format("releaseGateSelfCheck {} fails: {}", fails, report);
    return false;
}

int AssistController::resolveEffectTick(int attemptTick, bool midStep, bool continuous) {
    // Single time basis: DevClockContract (Vertrag C). Live and eval share this.
    return DevClockContract::get().effectTick(attemptTick, midStep, continuous);
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

    // Delegation parity with DevClockContract.
    expect(
        resolveEffectTick(10, false, false)
            == DevClockContract::get().effectTick(10, false, false),
        "delegate-discrete"
);
    expect(
        resolveEffectTick(10, false, true)
            == DevClockContract::get().effectTick(10, false, true),
        "delegate-cont"
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

    // Non-orb Delay must keep planned fire even when incidental act band is early.
    // (Mirrors planPress: only requireOrb may clamp to dMin/dMax.)
    {
        int const effect = 100;
        int const delay = 4;
        int const jump = 4;
        int fire = resolveFireTick(effect, delay, jump, false);
        expect(fire == 104, "non-orb-fire-at-delay");
        // Incidental orb band [0,1] must NOT collapse fire when !requireOrb.
        int const dMax = 1;
        int collapsed = std::min(fire, effect + dMax);
        expect(collapsed == 101, "demo-bad-clamp");
        expect(fire == 104, "non-orb-ignores-incidental-dmax");
    }

    if (fails == 0) {
        report = "fireTickSelfCheck OK";
        return true;
    }
    report = fmt::format("fireTickSelfCheck {} fails: {}", fails, report);
    return false;
}

int AssistController::resolveQueuedHoldTicks(
    bool requireHold,
    bool continuous,
    int releaseAfter,
    int releaseEarliest,
    int releaseLatest,
    int extendCap
) {
    if (extendCap < 0) extendCap = 0;

    int hold = -1;
    if (continuous) {
        // Earliest-safe only — never drag ceiling with blind +limit.
        if (releaseEarliest >= 0) hold = releaseEarliest;
        else if (releaseAfter >= 0) hold = releaseAfter;
        else return 0;
    }
    else if (releaseAfter >= 0) {
        hold = releaseAfter;
    }
    else if (requireHold) {
        hold = extendCap;
    }
    else {
        return 0;
    }

    if (hold <= 0) return 0;
    if (releaseLatest >= 0) hold = std::min(hold, releaseLatest);
    hold = std::min(hold, extendCap);
    return std::max(0, hold);
}

bool AssistController::queuedHoldSelfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    // Plain Delay tap + early release → ASAP (0 hold).
    expect(
        resolveQueuedHoldTicks(false, false, -1, -1, -1, 8) == 0,
        "plain-asap"
);
    expect(
        resolveQueuedHoldTicks(false, false, 0, -1, -1, 8) == 0,
        "rel0-asap"
);

    // Planner releaseAfter honored (discrete).
    expect(
        resolveQueuedHoldTicks(false, false, 5, 2, 10, 8) == 5,
        "rel-after"
);
    expect(
        resolveQueuedHoldTicks(true, false, 5, 2, 10, 8) == 5,
        "hold-rel-after"
);

    // requireHold without release fields → clamp to extendCap (dash/robot).
    expect(
        resolveQueuedHoldTicks(true, false, -1, -1, -1, 8) == 8,
        "hold-fallback-cap"
);

    // Continuous: earliest-safe only; no blind +limit.
    expect(
        resolveQueuedHoldTicks(true, true, 10, 2, 14, 16) == 2,
        "cont-earliest"
);
    expect(
        resolveQueuedHoldTicks(true, true, 6, -1, 14, 16) == 6,
        "cont-rel-after"
);
    expect(
        resolveQueuedHoldTicks(true, true, -1, -1, -1, 16) == 0,
        "cont-no-blind-cap"
);

    // Clamp to latest and extendCap.
    expect(
        resolveQueuedHoldTicks(false, false, 12, 2, 4, 16) == 4,
        "clamp-latest"
);
    expect(
        resolveQueuedHoldTicks(false, false, 12, 2, 20, 8) == 8,
        "clamp-cap"
);

    if (fails == 0) {
        report = "queuedHoldSelfCheck OK";
        return true;
    }
    report = fmt::format("queuedHoldSelfCheck {} fails: {}", fails, report);
    return false;
}

// Orb foresight or published Alive Delay-worthy delays — never bare PassHopeless.
static bool husHasEscape(SimPlan const& plan) {
    if (plan.foresightOrbPad || plan.usedOrbWindow) return true;
    return !plan.viableDelays.empty();
}


bool AssistController::shouldArmHoldUntilSafe(SimPlan const& plan, bool trustOk) {
    // HUS only when trusted — sonst Pass. Escape required (orb or Alive delay).
    if (!trustOk) return false;
    if (plan.action == PlanAction::Delay) return false;
    bool const escape = husHasEscape(plan);
    bool const opp = plan.foresightOrbPad || plan.usedOrbWindow;
    if (plan.reason == AssistReason::PassBudget) return escape;
    if (plan.budgetHit && opp) return true;
    if (plan.unknown && opp) return true;
    // Continuous idleSafer+ascent: Soft Hold only with Alive later / orb.
    if (plan.continuous && plan.idleSafer && plan.ascentUseful) return escape;
    // Hopeless after search: Soft Hold only with escape (no bare inject path).
    if (plan.reason == AssistReason::PassHopeless) return escape;
    return false;
}

int AssistController::holdUntilSafeEtaTicks(SimPlan const& plan, int limit) {
    if (limit <= 0) return 0;
    int eta = 0;
    if (plan.dMin > 0) {
        eta = plan.dMin;
    }
    else if (plan.delay > 0) {
        eta = plan.delay;
    }
    else {
        eta = std::max(1, limit / 2);
    }
    eta = MissAssistContracts::clampDelayToWindow(eta, limit);
    if (eta < 1) eta = 1;
    return eta;
}

bool AssistController::holdUntilSafeSelfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    SimPlan budgetPass;
    budgetPass.action = PlanAction::Pass;
    budgetPass.reason = AssistReason::PassBudget;
    expect(!shouldArmHoldUntilSafe(budgetPass, true), "no-arm-budget-bare");
    budgetPass.viableDelays = { 2 };
    expect(shouldArmHoldUntilSafe(budgetPass, true), "arm-budget-alive");
    expect(!shouldArmHoldUntilSafe(budgetPass, false), "no-hus-untrust");

    SimPlan delayOk;
    delayOk.action = PlanAction::Delay;
    delayOk.delay = 4;
    delayOk.reason = AssistReason::DelayThreat;
    expect(!shouldArmHoldUntilSafe(delayOk, true), "no-arm-delay");

    SimPlan noThreat;
    noThreat.action = PlanAction::Pass;
    noThreat.reason = AssistReason::PassNoThreat;
    expect(!shouldArmHoldUntilSafe(noThreat, true), "no-arm-no-threat");

    SimPlan foresightBudget;
    foresightBudget.action = PlanAction::Pass;
    foresightBudget.reason = AssistReason::PassHopeless;
    foresightBudget.budgetHit = true;
    foresightBudget.foresightOrbPad = true;
    expect(shouldArmHoldUntilSafe(foresightBudget, true), "arm-foresight-budget");

    SimPlan hopelessBare;
    hopelessBare.action = PlanAction::Pass;
    hopelessBare.reason = AssistReason::PassHopeless;
    expect(!shouldArmHoldUntilSafe(hopelessBare, true), "no-arm-hopeless-bare");
    hopelessBare.foresightOrbPad = true;
    expect(shouldArmHoldUntilSafe(hopelessBare, true), "arm-hopeless-orb");

    SimPlan unknownOrb;
    unknownOrb.action = PlanAction::Pass;
    unknownOrb.reason = AssistReason::PassD0Safe;
    unknownOrb.unknown = true;
    unknownOrb.usedOrbWindow = true;
    expect(shouldArmHoldUntilSafe(unknownOrb), "arm-unknown-orb");

    // Toggle-unknown + foresight → Soft Hold OK; plain toggle Pass → no HUS.
    SimPlan toggleUnk;
    toggleUnk.action = PlanAction::Pass;
    toggleUnk.reason = AssistReason::PassToggleUnknown;
    toggleUnk.unknown = true;
    toggleUnk.toggleUnknown = true;
    expect(!shouldArmHoldUntilSafe(toggleUnk), "no-arm-toggle-plain");
    toggleUnk.foresightOrbPad = true;
    expect(shouldArmHoldUntilSafe(toggleUnk), "arm-toggle-foresight");

    SimPlan idleAscent;
    idleAscent.action = PlanAction::Pass;
    idleAscent.reason = AssistReason::PassD0Safe;
    idleAscent.continuous = true;
    idleAscent.idleSafer = true;
    idleAscent.ascentUseful = true;
    expect(!shouldArmHoldUntilSafe(idleAscent, true), "no-arm-idle-ascent-bare");
    idleAscent.viableDelays = { 3 };
    expect(shouldArmHoldUntilSafe(idleAscent, true), "arm-idle-ascent-alive");
    idleAscent.continuous = false;
    expect(!shouldArmHoldUntilSafe(idleAscent, true), "no-arm-idle-cube");

    SimPlan etaPlan;
    etaPlan.dMin = 6;
    expect(holdUntilSafeEtaTicks(etaPlan, 4) == 4, "eta-clamp-6-to-4");
    expect(holdUntilSafeEtaTicks(etaPlan, 16) == 6, "eta-dmin-6");
    SimPlan midPlan;
    expect(holdUntilSafeEtaTicks(midPlan, 16) == 8, "eta-mid-16");
    expect(holdUntilSafeEtaTicks(midPlan, 0) == 0, "eta-limit0");

    // Cadence: first call / interval / ΔX / foresight edge / near deadline.
    expect(
        shouldReplanHoldUntilSafe(10, -1, 0.f, 0.f, false, false, 8),
        "replan-first"
);
    expect(
        !shouldReplanHoldUntilSafe(11, 10, 100.f, 100.f, false, false, 8),
        "replan-skip-1"
);
    expect(
        !shouldReplanHoldUntilSafe(12, 10, 100.f, 100.f, false, false, 8),
        "replan-skip-2"
);
    expect(
        shouldReplanHoldUntilSafe(13, 10, 100.f, 100.f, false, false, 8),
        "replan-every-3"
);
    expect(
        shouldReplanHoldUntilSafe(11, 10, 110.f, 100.f, false, false, 8),
        "replan-delta-x"
);
    expect(
        shouldReplanHoldUntilSafe(11, 10, 100.f, 100.f, true, false, 8),
        "replan-foresight-edge"
);
    expect(
        shouldReplanHoldUntilSafe(11, 10, 100.f, 100.f, false, false, 2),
        "replan-near-deadline"
);
    expect(kHusReplanEveryTicks == 3, "hus-every-const");
    expect(kHusReplanDeltaX == 8.f, "hus-dx-const");

    if (fails == 0) {
        report = "holdUntilSafeSelfCheck OK";
        return true;
    }
    report = fmt::format("holdUntilSafeSelfCheck {} fails: {}", fails, report);
    return false;
}

bool AssistController::shouldReplanHoldUntilSafe(
    int attemptTick,
    int lastReplanTick,
    float x,
    float lastReplanX,
    bool foresightNow,
    bool foresightLast,
    int remainingTicks
) {
    if (lastReplanTick < 0) return true;
    if (remainingTicks <= 2) return true;
    if (foresightNow != foresightLast) return true;
    if (std::fabs(x - lastReplanX) >= kHusReplanDeltaX) return true;
    if (attemptTick - lastReplanTick >= kHusReplanEveryTicks) return true;
    return false;
}

bool AssistController::armHoldUntilSafe(
    PlayLayer* playLayer,
    PlayerObject* player,
    bool player1,
    int effectTick,
    int limit,
    SimPlan const& plan,
    bool down
) {
 (void)playLayer;
    if (!player || limit <= 0) return true; // fall through vanilla
    if (!StageConfig::husSoftHoldEnabled()) return true; // disable-hus
    // HUS nur Trust.
    if (!SimTrust::get().okAt(player->getPositionX())) return true;

    int const eta = holdUntilSafeEtaTicks(plan, limit);
    this->m_buffer = {};
    this->m_buffer.state = BufferState::Buffered;
    this->m_buffer.down = down;
    this->m_buffer.player1 = player1;
    this->m_buffer.pressTick = effectTick;
    this->m_buffer.limit = limit;
    this->m_buffer.planDelay = eta;
    this->m_buffer.fireTick = effectTick + eta;
    this->m_buffer.expectedJumpTick = this->m_buffer.fireTick;
    this->m_buffer.continuous = plan.continuous;
    this->m_buffer.decisionX = player->getPositionX();
    this->m_buffer.mode = StageConfig::kindOf(player);
    this->m_buffer.requireOrb = plan.requireOrb || plan.foresightOrbPad || plan.usedOrbWindow;
    this->m_buffer.requireHold = plan.requireHold;
    this->m_buffer.releaseAfter = plan.releaseAfter;
    this->m_buffer.releaseEarliest = plan.releaseEarliest;
    this->m_buffer.releaseLatest = plan.releaseLatest;
    this->m_buffer.dMin = plan.dMin;
    this->m_buffer.dMax = plan.dMax;
    this->m_buffer.planId = plan.planId;
    this->m_buffer.holdUntilSafe = true;
    this->m_buffer.husLastReplanTick = -1; // force first cadence replan
    this->m_buffer.husLastReplanX = this->m_buffer.decisionX;
    this->m_buffer.husLastForesight = plan.foresightOrbPad || plan.usedOrbWindow;
    this->m_buffer.fireTick = std::min(
        this->m_buffer.fireTick, effectTick + limit
);
    this->beginShiftQueue(this->m_buffer.fireTick - effectTick);

    this->m_outcomePlanDelay = eta > 0;
    this->m_outcomeExpectOrb = this->m_buffer.requireOrb;
    {
        DevCycleRecorder::DelayArmMeta meta{};
        meta.armVerifyOk = true;
        meta.budgetHitAtArm = plan.budgetHit;
        this->m_buffer.devPressId = DevCycleRecorder::get().noteDelayArm(
            AssistReason::HoldPending,
            "hus-arm",
            eta,
            this->m_buffer.fireTick,
            plan.planId,
            DevEventSource::Live,
            meta
);
    }
    this->noteVerdict(AssistReason::HoldPending, "hus-arm");
    if (eta > 0) {
        AssistRealMetrics::get().noteArm(StageConfig::kindOf(player));
    }
    AssistLog::debug(fmt::format(
        "HOLD-UNTIL-SAFE arm {} eta={} limit={} foresight={} orbWin={} budgetHit={} id={}",
        down ? "press" : "release",
        eta, limit,
        plan.foresightOrbPad ? 1 : 0,
        plan.usedOrbWindow ? 1 : 0,
        plan.budgetHit ? 1 : 0,
        plan.planId
));
    this->recordDecision(playLayer, player, down, plan, effectTick, eta, limit);
    this->updateOverlay();
    return false; // swallow
}

void AssistController::tickHoldUntilSafe(PlayLayer* playLayer, PlayerObject* player) {
    if (!playLayer || !player) return;
    if (this->m_buffer.state != BufferState::Buffered) return;
    if (!this->m_buffer.holdUntilSafe) return;
    if (this->m_simDt <= 0.f) return;

    // Trust lost mid-HUS → Pass (drop soft hold, no inject).
    if (!SimTrust::get().okAt(player->getPositionX())) {
        AssistLog::debug("HOLD-UNTIL-SAFE abort (trust lost) → Pass");
        DevCycleRecorder::get().noteHus(
            this->m_buffer.devPressId,
            AssistReason::PassUntrust,
            "hus-trust-lost",
            this->m_attemptTick
);
        this->noteVerdict(AssistReason::PassUntrust, "hus-trust-lost");
        this->clearBuffer();
        return;
    }

    int const deadline = this->m_buffer.pressTick + this->m_buffer.limit;
    int const remaining = deadline - this->m_attemptTick;
    int const remLimit = std::max(1, remaining > 0 ? remaining : 1);
    float const x = player->getPositionX();
    // Cheap foresight edge (no full plan) so cadence can wake on orb/pad enter/leave.
    int const slack = StageConfig::deathSlack();
    bool const foresightNow = StageConfig::assistOrbs()
        && TravelForesightCache::get().hasOrbOrPadAhead(
            playLayer,
            player,
            remLimit,
            this->m_simDt,
            slack,
            this->m_attemptTick
);

    // Window end: inject only with orb escape still ahead — else drop (no inject).
    if (remaining <= 0 || this->m_attemptTick >= deadline) {
        bool const mayInject = this->m_buffer.requireOrb || foresightNow;
        if (!mayInject) {
            AssistLog::info(fmt::format(
                "HOLD-UNTIL-SAFE drop (no escape at deadline) @{} "
                "(press={} limit={})",
                this->m_attemptTick, this->m_buffer.pressTick, this->m_buffer.limit
));
            DevCycleRecorder::get().noteHus(
                this->m_buffer.devPressId,
                AssistReason::PassHopeless,
                "hus-drop",
                this->m_attemptTick
);
            this->noteVerdict(AssistReason::PassHopeless, "hus-drop");
            this->m_lastBakeHint = "HUS drop";
            this->clearBuffer();
            this->updateOverlay();
            return;
        }
        AssistLog::debug(fmt::format(
            "HOLD-UNTIL-SAFE deadline fire @{} (press={} limit={} orb={})",
            this->m_attemptTick, this->m_buffer.pressTick, this->m_buffer.limit,
            foresightNow ? 1 : 0
));
        DevCycleRecorder::get().noteHus(
            this->m_buffer.devPressId,
            AssistReason::HoldPending,
            "hus-deadline",
            this->m_attemptTick
);
        this->m_buffer.holdUntilSafe = false;
        this->m_buffer.fireTick = this->m_attemptTick;
        this->fireBuffered(playLayer, player, false);
        return;
    }

    if (!shouldReplanHoldUntilSafe(
            this->m_attemptTick,
            this->m_buffer.husLastReplanTick,
            x,
            this->m_buffer.husLastReplanX,
            foresightNow,
            this->m_buffer.husLastForesight,
            remaining
)) {
        this->m_buffer.fireTick = deadline;
        DevCycleRecorder::get().noteHus(
            this->m_buffer.devPressId,
            AssistReason::HoldPending,
            "hus-skip",
            this->m_buffer.fireTick
);
        this->noteVerdict(AssistReason::HoldPending, "hus-skip");
        LagProbe::get().mark(
            "hus.replan.skip",
            fmt::format("t={} rem={}", this->m_attemptTick, remaining)
);
        return;
    }

    // Never nest a full plan inside PlayerObject::update (Eclipse PauseBGLHook
    // coexistence / iOS ring SIGBUS). Physics-tick HUS already runs outside update;
    // this guards any future mid-step call site.
    if (this->m_inRealUpdate) {
        this->m_buffer.fireTick = deadline;
        DevCycleRecorder::get().noteHus(
            this->m_buffer.devPressId,
            AssistReason::HoldPending,
            "hus-defer-mid-update",
            this->m_buffer.fireTick
);
        this->noteVerdict(AssistReason::HoldPending, "hus-defer-mid-update");
        LagProbe::get().mark(
            "hus.replan.defer",
            fmt::format("t={} rem={}", this->m_attemptTick, remaining)
);
        return;
    }

    LagProbe::Scope const husScope("eb.hus.replan", fmt::format(
        "rem={} foresight={}", remaining, foresightNow ? 1 : 0
));

    PlanOptions opts;
    opts.budgetUs = StageConfig::husReplanBudgetUs();
    opts.tickBudget = StageConfig::husReplanTickBudget();
    opts.capMul = StageConfig::autoTune()
        ? LevelProfile::get().capMul(this->m_buffer.mode)
        : 1.f;
    opts.maxBranches = 2;
    opts.smartDepth = 1;
    opts.legacyPick = StageConfig::assistPickLegacy();
    opts.assistEngine = StageConfig::effectiveAssistEngine();
    opts.attemptStartTick = this->m_attemptTick;
    opts.requireAliveForDelay = true;
    fillPlanRobustLatency(opts, player, this->m_dtVarEma, true);

    bool const husDown = this->m_buffer.down;
    AssistSpikeWatch::get().beginSearch("hus");
    auto const husT0 = std::chrono::steady_clock::now();
    auto re = ShadowSimulator::get().plan(
        playLayer, player, husDown, remLimit, this->m_simDt, this->m_simDt, opts
);
    int const husUs = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - husT0
).count());
    AssistSpikeWatch::get().endSearch(husUs, re.budgetHit);
    AssistPerfStats::get().notePlan(husUs, re.budgetHit);

    this->m_buffer.husLastReplanTick = this->m_attemptTick;
    this->m_buffer.husLastReplanX = x;
    this->m_buffer.husLastForesight = foresightNow
        || re.foresightOrbPad || re.usedOrbWindow;

    if (re.action == PlanAction::Delay && re.delay > 0) {
        int delay = MissAssistContracts::clampDelayToWindow(re.delay, remLimit);
        if (delay < 1) delay = 1;
        int const jumpRel = re.jumpTick > 0 ? re.jumpTick : delay;
        int proposed = resolveFireTick(
            this->m_attemptTick, delay, jumpRel, re.requireOrb || this->m_buffer.requireOrb
);
        proposed = std::min(proposed, deadline);
        if (proposed < this->m_attemptTick) proposed = this->m_attemptTick;

        // Prefer not sliding earlier than current tentative fire unless orb window.
        if (!re.requireOrb && !this->m_buffer.requireOrb
            && proposed < this->m_buffer.fireTick
            && this->m_buffer.fireTick <= deadline) {
            proposed = this->m_buffer.fireTick;
        }

        this->m_buffer.fireTick = proposed;
        this->m_buffer.planDelay = MissAssistContracts::clampDelayToWindow(
            this->m_attemptTick - this->m_buffer.pressTick + delay,
            this->m_buffer.limit
);
        this->m_buffer.expectedJumpTick = this->m_attemptTick + jumpRel;
        this->m_buffer.requireOrb = this->m_buffer.requireOrb || re.requireOrb;
        this->m_buffer.requireHold = re.requireHold;
        this->m_buffer.releaseAfter = re.releaseAfter;
        this->m_buffer.releaseEarliest = re.releaseEarliest;
        this->m_buffer.releaseLatest = re.releaseLatest;
        if (re.dMin >= 0) this->m_buffer.dMin = re.dMin;
        if (re.dMax >= 0) this->m_buffer.dMax = re.dMax;
        this->m_buffer.holdUntilSafe = false; // hard Delay confirmed
        AssistReason delayReason = re.reason;
        if (!AssistVerdict::isDelay(delayReason)) {
            delayReason = AssistVerdict::inferDelayReason(
                re.activatedRing, re.requireOrb, false
);
        }
        DevCycleRecorder::get().noteHus(
            this->m_buffer.devPressId,
            delayReason,
            "hus-confirm",
            this->m_buffer.fireTick
);
        this->noteVerdict(delayReason, "hus-confirm");
        AssistLog::debug(fmt::format(
            "HOLD-UNTIL-SAFE confirm Delay +{} fire={} deadline={}",
            delay, this->m_buffer.fireTick, deadline
));
        return;
    }

    // Opportunity gone and now looks safe → inject now (never drop the click).
    bool const clearOpp = !re.foresightOrbPad && !re.usedOrbWindow && !re.budgetHit;
    bool const safeNow = re.reason == AssistReason::PassNoThreat
        || re.reason == AssistReason::PassD0Safe
        || (re.action == PlanAction::Pass && re.nowIsSafe && clearOpp);
    if (safeNow) {
        AssistLog::debug(fmt::format(
            "HOLD-UNTIL-SAFE flush Pass now @{} reason={}",
            this->m_attemptTick, AssistVerdict::name(re.reason)
));
        this->m_buffer.holdUntilSafe = false;
        this->m_buffer.fireTick = this->m_attemptTick;
        this->fireBuffered(playLayer, player, false);
        return;
    }

    // Budget exhaust / still uncertain: park at deadline (never hang).
    // PassBudget on HUS does not drop the click — window-cap fire later.
    this->m_buffer.fireTick = deadline;
    if (re.budgetHit || re.reason == AssistReason::PassBudget) {
        LagProbe::get().mark(
            "hus.replan.budget",
            fmt::format("t={} rem={}", this->m_attemptTick, remaining)
);
        DevCycleRecorder::get().noteHus(
            this->m_buffer.devPressId,
            AssistReason::HoldPending,
            "hus-budget",
            this->m_buffer.fireTick
);
        this->noteVerdict(AssistReason::HoldPending, "hus-budget");
        AssistLog::debug(fmt::format(
            "HOLD-UNTIL-SAFE budget exhaust → deadline={} (never hang)",
            deadline
));
    }
    else {
        DevCycleRecorder::get().noteHus(
            this->m_buffer.devPressId,
            AssistReason::HoldPending,
            "hus-wait",
            this->m_buffer.fireTick
);
        this->noteVerdict(AssistReason::HoldPending, "hus-wait");
    }
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
 (void)requireOrb; // same ≤+1 / never-earlier for orb + non-orb
    return OrbAlign::acceptRecheckSlide(
        oldFire, attemptTick, proposedFire, absMax, robustK, deadline, planOk
);
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

    expect(acceptRecheckSlide(10, 8, 12, 20, 1, 20, false, false) < 0, "reject-bad-plan");
    // Late slide capped at +1.
    expect(acceptRecheckSlide(10, 8, 20, 30, 0, 30, false, true) == 11, "cap-slide-plus1");
    expect(acceptRecheckSlide(15, 8, 11, 30, 0, 30, false, true) < 0, "early-slide-reject");
    // robustK: absMax 20, k=2 → safeMax 18, then +1 cap from oldFire=10 → 11
    expect(acceptRecheckSlide(10, 8, 19, 20, 2, 30, false, true) == 11, "robust-then-cap");
    expect(acceptRecheckSlide(10, 12, 12, 20, 0, 20, false, true) < 0, "not-future");
    // Orb/toggle: same ≤+1 / never earlier (dMin wait is a separate path)
    expect(acceptRecheckSlide(10, 8, 18, 20, 0, 20, true, true) == 11, "orb-slide-plus1");
    expect(acceptRecheckSlide(15, 8, 11, 30, 0, 30, true, true) < 0, "orb-no-earlier");
    expect(acceptRecheckSlide(12, 8, 12, 20, 0, 20, false, true) < 0, "noop-reject");
    expect(acceptRecheckSlide(10, 8, 14, 30, 0, 8, false, true) < 0, "deadline-collapse");
    expect(acceptRecheckSlide(10, 8, 14, 30, 0, 13, false, true) == 11, "deadline-cap-slide");

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
    if (m_buffer.state == BufferState::Buffered) {
        if (m_buffer.holdUntilSafe) state = "HUS";
        else state = m_buffer.down ? "buffer press" : "buffer release";
    }
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
        // Template ownership: last bake arm/miss hint over map status.
        // Hybrid/Replay/Brute prefix so Live foresight is never confused with tpl.
        std::string bakeBit = m_lastBakeHint.empty()
            ? BakeMap::get().statusLine()
            : m_lastBakeHint;
        if (!m_lastBakeHint.empty()) {
            char const* pref = nullptr;
            if (mode == AssistMode::Hybrid) pref = "hy";
            else if (mode == AssistMode::Replay) pref = "rp";
            else if (mode == AssistMode::BruteForce) pref = "br";
            if (pref) bakeBit = fmt::format("{} {}", pref, m_lastBakeHint);
        }
        // While a bake-armed buffer is waiting, mark tpl ownership on left=.
        if (m_buffer.state == BufferState::Buffered && m_buffer.fromBake) {
            bakeBit = fmt::format("{} · tpl{}", bakeBit, m_buffer.down ? "↓" : "↑");
        }
        std::string queueBit;
        if (this->m_shiftQueue.active()
            && (this->m_shiftQueue.size() > 0
                || this->m_buffer.state == BufferState::Buffered
                || this->m_shiftDrainPass)) {
            queueBit = fmt::format(
                "queue={} shift={}",
                this->m_shiftQueue.size(),
                this->m_shiftQueue.shift()
            );
            if (this->m_shiftDrainPass) queueBit += " drain";
        }
        std::string trajBit = TrajectoryVisualizer::get().statusHint();
        std::string bakeVizBit = BakePathVisualizer::get().statusHint();
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
        if (!queueBit.empty()) m_overlayText += " | " + queueBit;
        if (!cpsBit.empty()) m_overlayText += " | " + cpsBit;
        if (!trajBit.empty()) m_overlayText += " | " + trajBit;
        if (!bakeVizBit.empty()) m_overlayText += " | " + bakeVizBit;
        if (StageConfig::triggerBake()) {
            if (TriggerWorldBaker::get().running()) {
                this->m_overlayText += fmt::format(
                    " | tw building {:.0f}%",
                    TriggerWorldBaker::get().progress() * 100.f
);
            }
            else {
                auto& tw = TriggerWorldStore::get();
                if (!tw.loaded()) {
                    this->m_overlayText += " | tw off";
                }
                else if (!tw.hasTracks()) {
                    this->m_overlayText += " | tw empty";
                }
                else if (tw.softcapHit()) {
                    this->m_overlayText += fmt::format(
                        " | tw {}/{} softcap",
                        tw.readyTrackCount(),
                        tw.trackCount()
);
                }
                else {
                    this->m_overlayText += fmt::format(
                        " | tw {}/{}",
                        tw.readyTrackCount(),
                        tw.trackCount()
);
                }
            }
        }
        if (StageConfig::assistOrbs()) m_overlayText += " | orbs";
        this->m_overlayText += fmt::format(" | pick={}", StageConfig::assistPickName());
        this->m_overlayText += fmt::format(
            " | eng={}",
            StageConfig::assistEngineName()
);
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
            auto bit = AssistSpikeWatch::get().overlayBit();
            if (!bit.empty()) m_overlayText += " | " + bit;
        }
        {
            auto bit = DivergenceTracker::get().overlayBit();
            if (!bit.empty()) m_overlayText += " | " + bit;
            if (m_untrustPassHint) m_overlayText += " | UNTRUST pass";
        }
        {
            char const* const trustBit = SimTrust::get().overlayBit();
            if (trustBit && trustBit[0] != '\0') {
                this->m_overlayText += " | ";
                this->m_overlayText += trustBit;
            }
        }
        {
            // Real metrics primary; simEstimate secondary (G10).
            auto const metricsBit = AssistRealMetrics::get().overlayBit();
            if (!metricsBit.empty()) {
                this->m_overlayText += " | " + metricsBit;
            }
            auto const simBit = AssistSimEstimate::get().overlayBit();
            if (!simBit.empty()) {
                this->m_overlayText += " | " + simBit;
            }
        }
        {
            auto const cacheBit = LevelCacheIndex::overlayBit();
            if (!cacheBit.empty()) {
                this->m_overlayText += " | " + cacheBit;
            }
        }
        if (m_savesAttempt > 0) {
            m_overlayText += fmt::format(" | saves {}/{}", m_savesAttempt, m_savesLevel);
        }
        if (m_buffer.requireOrb && m_buffer.dMin >= 0) {
            m_overlayText += fmt::format(" | orbWin[{},{}]", m_buffer.dMin, m_buffer.dMax);
        }
        {
            char const* const verdictBit = AssistVerdict::overlayBit(this->m_lastVerdict);
            if (verdictBit && verdictBit[0] != '\0') {
                this->m_overlayText += " | ";
                this->m_overlayText += verdictBit;
            }
        }
    }
    if (DevOverlay::shouldShow()) {
        auto const devLine = DevOverlay::line();
        if (!devLine.empty()) {
            if (!this->m_overlayText.empty()) this->m_overlayText += " || ";
            this->m_overlayText += devLine;
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
    bool const orbIntent = plan.requireOrb || plan.activatedRing || plan.usedOrbWindow;
    AssistLog::debug(fmt::format(
        "PLAN id={} kind={} action={} verdict={} delay={} orbIntent={} foresight={} eng={} requireOrb={} hold={} dMin={} dMax={} budgetHit={}",
        plan.planId, kind,
        plan.action == PlanAction::Pass ? "Pass" : "Delay",
        AssistVerdict::name(plan.reason),
        plan.delay,
        orbIntent ? 1 : 0,
        plan.foresightOrbPad ? 1 : 0,
        plan.trajectoryEngine ? "Trajectory" : "ShadowPlayer",
        plan.requireOrb, plan.requireHold, plan.dMin, plan.dMax, plan.budgetHit
));
}

void AssistController::noteVerdict(AssistReason reason, char const* where) {
    this->m_lastVerdict = reason;
    AssistLog::debug(fmt::format(
        "VERDICT {} where={} overlay={}",
        AssistVerdict::name(reason),
        where ? where : "?",
        AssistVerdict::overlayBit(reason)
));
}

void AssistController::noteNoEscapeHonesty(SimPlan const& plan, char const* where) {
    if (husHasEscape(plan)) return;
    if (plan.reason != AssistReason::PassHopeless
        && plan.reason != AssistReason::PassBudget) {
        return;
    }
    this->m_lastBakeHint = "PASS no-escape";
    AssistLog::info(fmt::format(
        "ASSIST {} PASS no-escape id={} reason={} lfBothDead={} "
        "idleSafer={} ascent={}",
        where ? where : "?",
        plan.planId,
        AssistVerdict::name(plan.reason),
        plan.longForesightBothDead ? 1 : 0,
        plan.idleSafer ? 1 : 0,
        plan.ascentUseful ? 1 : 0
));
}

void AssistController::onRealDeath(PlayLayer* playLayer, PlayerObject* player, GameObject* object) {
    if (!playLayer || !player) return;
    if (ShadowSimulator::get().isSimulating()) return;
    if (ShadowSimulator::get().isOwnedGhost(player)) return;
 (void)object;

    LagProbe::Scope const deathScope("eb.onRealDeath");
    HybridVerifyCache::get().clearAttempt();

    // destroyPlayer hot path — no physics replays, no disk, no bake merge.
    // GD physics is main-thread only; "multithread" death work is not viable.
    // Heavy learning/bake runs on resetAttempt (next attempt), not here.
    // Drop in-flight trigger-world slices (no flush); partial store may remain.
    TriggerWorldBaker::get().cancel();
    if (this->m_dafWatch) {
        AssistRealMetrics::get().noteDeathAfterFire(this->m_dafWatchMode);
        this->m_dafWatch = false;
        this->m_dafWatchMode = GameModeKind::Cube;
    }
    if (this->m_outcomePending) {
        LagProbe::Scope const s("eb.outcomeNote");
        DivergenceTracker::get().note(
            this->m_outcomePlanDelay, true, this->m_outcomeExpectOrb, false
);
        this->m_outcomePending = false;
    }

    // Seal Dev attempt on death (outcomes + flush). Next attempt opens on reset.
    {
        LagProbe::Scope const s("eb.devSealDeath");
        DevCycleRecorder::get().sealAttemptWithOutcomes(
            "death", false, this->m_attemptTick
);
    }
    if (DevConfig::evalOnDeath() && DevCycleRecorder::get().isRecording()) {
        std::string evalErr;
        if (DevReport::evalLastAttempt(evalErr)) {
            this->updateOverlay();
        }
        else if (!evalErr.empty()) {
            AssistLog::debug(fmt::format("Dev Lab eval-on-death: {}", evalErr));
        }
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
    this->m_pendingReleaseHold = false;
    this->m_pendingReleaseContinuous = false;
    this->m_pendingReleaseArmTick = 0;
    this->m_pendingReleaseUserUpTick = -1;
    this->m_pendingReleaseMaxExtend = 0;
    this->m_pendingReleaseTick = 0;
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

    bool const p1 = this->m_pendingReleaseP1;
    bool const holding = this->userHolding(p1);
    auto* p = p1 ? playLayer->m_player1 : playLayer->m_player2;

    // HARD RULE: while the user still holds Jump, never inject a release.
    if (holding) {
        return;
    }

    // First physics tick after user-up: start the extend budget (max X ticks).
    if (this->m_pendingReleaseUserUpTick < 0) {
        this->m_pendingReleaseUserUpTick = this->m_attemptTick;
        this->m_pendingReleaseTick =
            this->m_pendingReleaseUserUpTick + this->m_pendingReleaseMaxExtend;
        AssistLog::debug(fmt::format(
            "hold-assist: user-up @{} extend≤{} deadline={}",
            this->m_pendingReleaseUserUpTick,
            this->m_pendingReleaseMaxExtend,
            this->m_pendingReleaseTick
));
    }

    bool gameDown = false;
    if (p) {
        auto it = p->m_holdingButtons.find(static_cast<int>(PlayerButton::Jump));
        gameDown = it != p->m_holdingButtons.end() && it->second;
    }
    if (!gameDown) {
        this->m_pendingRelease = false;
        this->m_pendingReleaseHold = false;
        this->m_pendingReleaseContinuous = false;
        this->m_pendingReleaseUserUpTick = -1;
        this->m_pendingReleaseMaxExtend = 0;
        return;
    }

    // Still inside the post-release extend window — keep Jump down.
    if (this->m_attemptTick < this->m_pendingReleaseTick) {
        return;
    }

    this->m_pendingRelease = false;
    this->m_pendingReleaseHold = false;
    this->m_pendingReleaseContinuous = false;
    this->m_pendingReleaseUserUpTick = -1;
    this->m_pendingReleaseMaxExtend = 0;
    this->m_injecting = true;
    playLayer->handleButton(false, JUMP_BUTTON, p1);
    this->m_injecting = false;
    AssistLog::debug(fmt::format(
        "hold-assist: injected release @{} (p{}) [extend done]",
        this->m_attemptTick, p1 ? 1 : 2
));
}

void AssistController::fireBuffered(
    PlayLayer* playLayer, PlayerObject* player, bool allowRecheck
) {
    int const elapsed = this->m_attemptTick - this->m_buffer.pressTick;
    int const remaining = this->m_buffer.limit - elapsed;

    // Soft path (physics due / recheck): never inject before fireTick.
    // Force paths (CPS / mode-change / HUS deadline) pass allowRecheck=false.
    if (allowRecheck
        && this->m_buffer.fireTick > this->m_attemptTick
        && remaining > 0) {
        AssistLog::debug(fmt::format(
            "fireBuffered: not due yet attempt={} fire={} — wait",
            this->m_attemptTick, this->m_buffer.fireTick
));
        return;
    }

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
            DevCycleRecorder::get().noteRecheck(
                this->m_buffer.devPressId, oldFire, absMin, "orb-dmin"
);
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
            opts.attemptStartTick = this->m_attemptTick;
            opts.capMul = StageConfig::autoTune()
                ? LevelProfile::get().capMul(this->m_buffer.mode)
                : 1.f;
            opts.maxBranches = 2;
            opts.smartDepth = 1;
            opts.legacyPick = StageConfig::assistPickLegacy();
            opts.assistEngine = StageConfig::effectiveAssistEngine();
            opts.requireAliveForDelay = true;
            fillPlanRobustLatency(opts, player, this->m_dtVarEma, true);
            auto re = ShadowSimulator::get().plan(
                playLayer, player, this->m_buffer.down, remaining,
                this->m_simDt, this->m_simDt, opts
);

            // Surviving Delay only — never slide into a lethal / hopeless replan.
            bool const planOk =
                re.action == PlanAction::Delay
                && re.delay > 0
                && !re.budgetHit
                && re.threatCleared
                && (re.requireOrb || this->m_buffer.requireOrb || !re.unknown);

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
                    DevCycleRecorder::get().noteRecheck(
                        this->m_buffer.devPressId, oldFire, accepted, "slide"
);
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
    int const releaseAfter = this->m_buffer.releaseAfter;
    int const releaseEarliest = this->m_buffer.releaseEarliest;
    int const releaseLatest = this->m_buffer.releaseLatest;
    bool const letGo = this->m_buffer.releaseSeen || !this->userHolding(player1);
    int const delayed = std::max(0, this->m_attemptTick - this->m_buffer.pressTick);
    int const planId = this->m_buffer.planId;
    bool const expectOrb = this->m_buffer.requireOrb;
    int64_t const devPressId = this->m_buffer.devPressId;
    // Max post-release / queued hold = this press's tick window (e.g. Ticks=4 → ≤4).
    int const extendCap = std::max(0, this->m_buffer.limit);
    int const queuedHold = resolveQueuedHoldTicks(
        requireHold, continuous, releaseAfter, releaseEarliest, releaseLatest, extendCap
);

    DevCycleRecorder::get().noteFire(devPressId, this->m_attemptTick);

    this->inject(playLayer, this->m_buffer.down);
    // Real-harm watch: death after a fired Delay inject (DAF).
    if (delayed > 0 && wasPress) {
        this->m_dafWatch = true;
        this->m_dafWatchMode = player
            ? StageConfig::kindOf(player)
            : this->m_buffer.mode;
    }
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

    // Keep same-shift follow-ups; drop the queue if nothing was enqueued.
    this->clearBuffer(/*keepShiftQueue=*/true);
    if (this->m_shiftQueue.empty() && !this->m_shiftDrainPass) {
        this->m_shiftQueue.clear();
    }

    // After assisted press:
    // - While user holds: never force-release.
    // - User already up (releaseSeen) + plan hold → queued hold in GD for N ticks, then UP.
    // - User already up + plain Delay → ASAP release (swallowed early UP).
    // - User still holding + requireHold → extend-after-release on their UP.
    if (wasPress) {
        if (letGo) {
            this->m_pendingRelease = true;
            this->m_pendingReleaseP1 = player1;
            this->m_pendingReleaseArmTick = this->m_attemptTick;
            this->m_pendingReleaseUserUpTick = this->m_attemptTick;
            if (queuedHold > 0) {
                // Queued click hold: inject stays down for planned ticks despite early UP.
                this->m_pendingReleaseHold = requireHold || releaseAfter > 0;
                this->m_pendingReleaseContinuous = continuous;
                this->m_pendingReleaseMaxExtend = queuedHold;
                this->m_pendingReleaseTick = this->m_attemptTick + queuedHold;
                AssistLog::debug(fmt::format(
                    "hold-assist: arm queued hold={} @fire={} hold={} cont={} rel=[{},{}]@{}",
                    queuedHold,
                    this->m_attemptTick,
                    requireHold ? 1 : 0,
                    continuous ? 1 : 0,
                    releaseEarliest,
                    releaseLatest,
                    releaseAfter
));
                DevCycleRecorder::get().noteHoldExtend(devPressId, queuedHold);
            }
            else {
                this->m_pendingReleaseHold = false;
                this->m_pendingReleaseContinuous = false;
                this->m_pendingReleaseMaxExtend = 0;
                this->m_pendingReleaseTick = this->m_attemptTick; // flush ASAP
                AssistLog::debug(fmt::format(
                    "hold-assist: arm release ASAP (user already up) @fire={}",
                    this->m_attemptTick
));
            }
        }
        else if (requireHold) {
            // Only explicit requireHold (dash orb / robot hold) gets post-release extend.
            // Continuous modes (Wave/Ship/Swing) plan releases live in planRelease so
            // letting go never drags the player into ceiling hazards for +limit ticks.
            int const extend = queuedHold > 0 ? queuedHold : extendCap;
            this->m_pendingRelease = true;
            this->m_pendingReleaseP1 = player1;
            this->m_pendingReleaseHold = requireHold;
            this->m_pendingReleaseContinuous = continuous;
            this->m_pendingReleaseArmTick = this->m_attemptTick;
            this->m_pendingReleaseUserUpTick = -1;
            this->m_pendingReleaseMaxExtend = extend;
            this->m_pendingReleaseTick = 0; // set on user-up
            AssistLog::debug(fmt::format(
                "hold-assist: arm extend-after-release max={} @fire={} hold={} cont={}",
                extend,
                this->m_attemptTick,
                requireHold ? 1 : 0,
                continuous ? 1 : 0
));
            DevCycleRecorder::get().noteHoldExtend(devPressId, extend);
        }
        // else: plain Delay tap — user owns hold+release fully (no pending).
    }
}

void AssistController::onPhysicsTick(PlayLayer* playLayer, float dt) {
    PerfTrace::get().beginTick(this->m_attemptTick, dt);
    // endTick runs on every exit path (including early returns).
    struct TickEndGuard {
        ~TickEndGuard() { PerfTrace::get().endTick(); }
    } tickEndGuard;

    float tw = 1.f;
    // Hook dt is GD step units (0.25 @ 240 TPS every tick — not a hitch).
    // Convert to seconds before sanity / EMA (v1.2.1 passed GD units through;
    // we keep seconds internally for TPS/bake and reconvert at the ghost).
    float const secDt = AssistClock::gdStepToSeconds(dt);
    if (secDt > 0.f) {
        tw = playLayer ? playLayer->m_gameState.m_timeWarp : 1.f;
        if (tw <= 0.f) tw = 1.f;
        float const simSample = secDt / tw;
        // Real-seconds hitch/pause dumps must NOT stick in m_lastDt.
        auto& clock = AssistClock::get();
        if (clock.isSaneSimDt(simSample)) {
            if (this->m_lastDt <= 0.f || this->m_attemptTick < 3) {
                this->m_lastDt = secDt;
            }
            else {
                this->m_lastDt += 0.2f * (secDt - this->m_lastDt);
            }
            this->m_simDt = this->m_lastDt / tw;
        }
        else if (this->m_simDt <= 0.f || !clock.isSaneSimDt(this->m_simDt)) {
            this->m_simDt = AssistClock::kNominalSimDt;
            this->m_lastDt = this->m_simDt * tw;
        }
        float const beforeSnap = this->m_simDt;
        this->m_simDt = clock.planSimDtOrNominal(this->m_simDt, tw);
        if (std::fabs(this->m_simDt - beforeSnap) > 1e-7f) {
            // Keep lastDt coherent with snapped planning quantum.
            this->m_lastDt = this->m_simDt * tw;
        }

        float const err = secDt - this->m_dtEma;
        this->m_dtEma += 0.1f * err;
        float const denom = std::max(this->m_dtEma, 1e-4f);
        this->m_dtVarEma = 0.1f * std::abs(err) / denom + 0.9f * this->m_dtVarEma;
    }
    ++this->m_attemptTick;
    {
        double const simTps = AssistClock::get().simTpsFromDt(this->m_simDt);
        if (AssistClock::get().noteSpeedScale(tw, simTps, this->m_attemptTick)) {
            AssistLog::debug(fmt::format(
                "speed-scale: suspect simTps={:.1f} wallTps={:.1f} tw={:.3f} simDt={:.6f} lastDt={:.6f}",
                simTps,
                AssistClock::get().wallTpsFromDt(this->m_lastDt),
                tw,
                this->m_simDt,
                this->m_lastDt
));
            LagProbe::get().mark(
                "eb.speed.scale",
                fmt::format("tps={:.1f} tw={:.3f}", simTps, tw)
);
        }
    }
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
        PerfTrace::Scope const scope("shiftQueue");
        this->tickShiftQueue(playLayer);
    }
    {
        PerfTrace::Scope const scope("saveFeedbackTick");
        SaveFeedback::get().onPhysicsTick();
    }
    {
        PerfTrace::Scope const scope("selfBakeIdle");
        SelfBakeRecorder::get().onPhysicsTick(playLayer);
    }
        {
            PerfTrace::Scope const scope("triggerBakeSlice");
            TriggerWorldBaker::get().kickIfStaleEmpty(playLayer);
            TriggerWorldBaker::get().tickSlice();
        }

    auto* p1 = playLayer->m_player1;
    if (p1) {
        {
            PerfTrace::Scope const scope("ghostParity");
            GhostParityProbe::get().onPhysicsTick(
                playLayer, p1, this->m_simDt, this->m_attemptTick
);
        }
        // Drift mid-buffer → drop Delay; never fire a plan made under bad trust.
        if (SimTrust::get().parityDrift()
            && this->m_buffer.state == BufferState::Buffered) {
            AssistLog::debug("parity drift → clear buffer (force Pass)");
            this->clearBuffer();
            this->syncButtonState(playLayer);
            this->noteVerdict(AssistReason::PassParityDrift, "parity-clear-buffer");
        }
        this->m_lastPlayerX = p1->getPositionX();
        this->m_lastMode = StageConfig::kindOf(p1);
        if (!this->m_startSnapshotTaken && this->m_attemptTick <= 2) {
            PerfTrace::Scope const scope("captureStartSnapshot");
            this->m_startSnapshotSlot = ShadowSimulator::get().captureStartSnapshot(p1);
            this->m_startSnapshotTaken = this->m_startSnapshotSlot >= 0;
            if (this->m_startSnapshotTaken) {
                float startX = 0.f;
                if (auto* snap = ShadowSimulator::get().snapshotSlot(
                        this->m_startSnapshotSlot
                    )) {
                    startX = snap->getPositionX();
                }
                BakeMap::get().setStartXq(LevelFingerprint::startXq(startX));
            }
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
    {
        PerfTrace::Scope const scope("bakePath");
        BakePathVisualizer::get().onPhysicsTick(playLayer, dt);
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
        if (this->m_buffer.holdUntilSafe) {
            PerfTrace::Scope const scope("tickHoldUntilSafe");
            this->tickHoldUntilSafe(playLayer, player);
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
    }
    else if (this->m_buffer.state != BufferState::Idle) {
        this->clearBuffer();
    }
    this->updateOverlay();
}

bool AssistController::tryBakeAssist(
    PlayLayer* playLayer, PlayerObject* player, bool player1,
    bool down, int effectTick, float firstDt, bool& handled,
    PressSimBudget* sharedBudget,
    bool* verifyAttempted,
    HybridPressReuse* reuse
) {
    handled = false;
    if (verifyAttempted) *verifyAttempted = false;
    if (reuse) *reuse = {};
    auto mode = StageConfig::assistMode();
    if (mode == AssistMode::Live) return true;

    bool const bakeOnly =
        mode == AssistMode::Replay || mode == AssistMode::BruteForce;
    bool const hybrid = mode == AssistMode::Hybrid;
    float const playerX = player->getPositionX();

    // Loud miss line for Hybrid/Replay when a bake window was in play but we
    // did not arm (screenshot bug: green box, press, nothing).
    auto logMissBakeWin = [&](
        char const* branch,
        BakedClick const* w,
        int earliestTick,
        int latestTick,
        int delay,
        int approxFrame
) {
        float const clickX = w ? w->x : -1.f;
        float const dx = w ? std::fabs(clickX - playerX) : -1.f;
        AssistLog::info(fmt::format(
            "MISS-BAKE-WIN {} {} pX={:.1f} clickX={:.1f} dx={:.1f} "
            "approxF={} nom={} eF={} lF={} effectT={} eT={} lT={} delay={} "
            "mode={} hybrid={}",
            branch,
            down ? "press" : "release",
            playerX,
            clickX,
            dx,
            approxFrame,
            w ? w->nominalFrame : -1,
            w ? w->earliest : -1,
            w ? w->latest : -1,
            effectTick,
            earliestTick,
            latestTick,
            delay,
            StageConfig::kindName(StageConfig::kindOf(player)),
            hybrid ? 1 : 0
));
        this->m_lastBakeHint = fmt::format("MISS {}", branch);
    };

    auto fallThroughOrPass = [&](char const* reason, AssistReason bakeReason) -> bool {
        AssistLog::debug(fmt::format("bake: {} → {}", reason, hybrid ? "live" : "pass"));
        if (hybrid) {
            handled = false;
            // Keep MISS hint if already set; else stamp fallthrough.
            if (this->m_lastBakeHint.rfind("MISS", 0) != 0) {
                this->m_lastBakeHint = fmt::format("{} → live", reason);
            }
            else {
                this->m_lastBakeHint = fmt::format("{} → live", this->m_lastBakeHint);
            }
            AssistLog::debug(fmt::format(
                "bake hybrid: {} — fall through to live (foresight/miss-assist)",
                reason
));
            this->updateOverlay();
            return true;
        }
        handled = true;
        if (this->m_lastBakeHint.rfind("MISS", 0) != 0) {
            this->m_lastBakeHint = reason;
        }
        DevCycleRecorder::get().noteGatePass(bakeReason, reason);
        this->noteVerdict(bakeReason, reason);
        return true;
    };

    auto const& bake = BakeMap::get();
    if (!bake.loaded() || bake.data().clicks.empty()) {
        if (bakeOnly) {
            handled = true;
            this->m_lastBakeHint = mode == AssistMode::BruteForce ? "brute missing" : "bake missing";
            DevCycleRecorder::get().noteGatePass(AssistReason::PassBakeMissing, "bake-missing");
            this->noteVerdict(AssistReason::PassBakeMissing, "bake-missing");
            return true;
        }
        return fallThroughOrPass("bake missing", AssistReason::PassBakeMissing);
    }

    double tps = bake.data().tps > 0.0 ? bake.data().tps : AssistClock::kNominalTps;
    int approxFrame = AssistClock::get().remapTick(tps, this->m_attemptTick, this->m_simDt);
    auto const kind = StageConfig::kindOf(player);
    auto const* win = bake.findNear(playerX, approxFrame, down, !player1, kind);
    if (!win) {
        // Only loud-miss when a drawn/nearby window exists but findNear skipped it
        // (mode/frame filter) — not every press far from bake.
        BakedClick const* spatialNear = nullptr;
        float bestDx = BakeMap::LOOKUP_X_TOL + 1.f;
        for (auto const& c : bake.data().clicks) {
            if (c.down != down || c.player2 != (!player1)) continue;
            float const dx = std::fabs(c.x - playerX);
            float const halfW = BakeMap::windowHalfW(c.earliest, c.latest);
            if (dx > BakeMap::LOOKUP_X_TOL && dx > halfW) continue;
            if (dx < bestDx) {
                bestDx = dx;
                spatialNear = &c;
            }
        }
        if (spatialNear) {
            logMissBakeWin(
                "no-window", spatialNear, -1, -1, -1, approxFrame
);
        }
        if (bakeOnly) {
            handled = true;
            this->m_lastBakeHint = spatialNear ? "MISS no-window" : "no window";
            DevCycleRecorder::get().noteGatePass(AssistReason::PassBakeNoWindow, "bake-no-window");
            this->noteVerdict(AssistReason::PassBakeNoWindow, "bake-no-window");
            return true;
        }
        return fallThroughOrPass("no window", AssistReason::PassBakeNoWindow);
    }

    // Copy window fields for Live fallthrough (no raw bake-vector pointers).
    if (reuse) {
        reuse->hasWindow = true;
        reuse->earliest = win->earliest;
        reuse->latest = win->latest;
        reuse->nominalFrame = win->nominalFrame;
        reuse->bakeTps = tps;
    }

    float const halfW = BakeMap::windowHalfW(win->earliest, win->latest);
    bool const spatialIn = std::fabs(playerX - win->x) <= halfW;

    // ABSOLUTE bake frame → live attempt tick (inverse of remapTick).
    // Old relative mapping (effectTick + frame - nominal) recentered the window
    // on the press: with LOOKUP_X_TOL≈120px (~100f early) that fired immediately
    // while still far before earliest — green box, press, death.
    float const simDt = this->m_simDt > 0.f
        ? this->m_simDt
        : static_cast<float>(1.0 / AssistClock::kNominalTps);
    auto& clock = AssistClock::get();
    int earliestTick = clock.unmapTick(tps, win->earliest, simDt);
    int latestTick = clock.unmapTick(tps, win->latest, simDt);
    int const nominalTick = clock.unmapTick(tps, win->nominalFrame, simDt);
    if (latestTick < earliestTick) std::swap(latestTick, earliestTick);

    int const limit = activeLimit(player);

    // Past latest: only salvage a tiny tick slop while still in the drawn band.
    // Deeply late + still spatially overlapping a wide box used to arm and die.
    static constexpr int kPastLatestSalvageSlop = 2;
    bool salvagedPastLatest = false;
    if (effectTick > latestTick) {
        int const lateBy = effectTick - latestTick;
        if (spatialIn && lateBy <= kPastLatestSalvageSlop) {
            salvagedPastLatest = true;
            AssistLog::info(fmt::format(
                "MISS-BAKE-WIN past-latest→salvage-now {} pX={:.1f} clickX={:.1f} "
                "dx={:.1f} halfW={:.1f} effectT={} lT={} lateBy={} eF={} lF={}",
                down ? "press" : "release",
                playerX, win->x, std::fabs(playerX - win->x), halfW,
                effectTick, latestTick, lateBy, win->earliest, win->latest
));
        }
        else {
            logMissBakeWin(
                lateBy > kPastLatestSalvageSlop ? "past-latest-deep" : "past-latest",
                win, earliestTick, latestTick, -1, approxFrame
);
            return fallThroughOrPass("past latest", AssistReason::PassBakePastLatest);
        }
    }

    // Absolute template delay:
    //  - before earliest → wait until earliest (never fire early of window)
    //  - inside window but before nominal → wait until nominal (replay center)
    //  - at/after nominal (still ≤ latest) or salvaged → fire now
    // If assist limit cannot reach earliest, do not clamp-fire early — miss instead.
    int delay = 0;
    if (!salvagedPastLatest) {
        if (effectTick < earliestTick) {
            delay = earliestTick - effectTick;
            if (limit > 0 && delay > limit) {
                logMissBakeWin(
                    "limit-before-earliest", win, earliestTick, latestTick,
                    delay, approxFrame
);
                AssistLog::info(fmt::format(
                    "MISS-BAKE-WIN limit-before-earliest need={} lim={} "
                    "effectT={} eT={} — refuse early fire",
                    delay, limit, effectTick, earliestTick
));
                return fallThroughOrPass(
                    "limit before earliest", AssistReason::PassBakeNoWindow
);
            }
        }
        else if (effectTick < nominalTick) {
            // Full wait to nominal — do NOT clamp to ticks-all/mode limit.
            // Clamping fired mid-window (early) and made verify use the wrong
            // delay → Hybrid fallthrough → HUS even earlier. Buffer.limit is
            // raised below to cover this wait (template > live tick budget).
            delay = nominalTick - effectTick;
            if (effectTick + delay > latestTick) {
                delay = std::max(0, latestTick - effectTick);
            }
        }
        if (delay < 0) delay = 0;
    }

    auto runVerify = [&](int d, BakeVerifier::Result& outVr) -> void {
        float const fd = (firstDt > 0.f && firstDt <= this->m_simDt)
            ? firstDt
            : this->m_simDt;
        if (verifyAttempted) *verifyAttempted = true;

        HybridVerifyCache::Key cacheKey;
        cacheKey.nominalFrame = win->nominalFrame;
        cacheKey.earliest = win->earliest;
        cacheKey.latest = win->latest;
        cacheKey.delayTicks = d;
        cacheKey.limit = limit;
        cacheKey.down = down;
        cacheKey.player2 = !player1;
        cacheKey.mode = kind;

        bool cacheHit = false;
        if (hybrid) {
            cacheHit = HybridVerifyCache::get().tryGet(
                cacheKey, playerX, outVr
);
        }
        if (cacheHit) {
            LagProbe::Scope const hyVerify(
                hybrid ? "eb.hybrid.verify" : nullptr,
                fmt::format(
                    "d={} lim={} cache=1 ok={}", d, limit, outVr.ok ? 1 : 0
)
);
 (void)hyVerify;
            return;
        }
        LagProbe::Scope const hyVerify(
            hybrid ? "eb.hybrid.verify" : nullptr,
            fmt::format("d={} lim={} cache=0", d, limit)
);
        outVr = BakeVerifier::verify(
            playLayer, player, down, d, limit, this->m_simDt, fd,
            hybrid ? sharedBudget : nullptr
);
        if (hybrid) {
            HybridVerifyCache::get().put(cacheKey, playerX, outVr);
        }
 (void)hyVerify;
    };

    // Live re-verify. Never soft-trust budgetHit.
    // d0 salvage only at/after nominal — before nominal it would fire early
    // (Hybrid then Live/HUS made that worse: pass-hopeless → mid-window inject).
    // Still fail before nominal → Pass (no Live). At/after nominal → Hybrid Live.
    BakeVerifier::Result vr{};
    vr.ok = true;
    if (limit > 0 && this->m_simDt > 0.f) {
        runVerify(delay, vr);
        if (reuse) reuse->verifyThreat = vr.threatZone;

        bool const beforeNominal = effectTick < nominalTick;
        if (!vr.ok && delay > 0 && !beforeNominal) {
            BakeVerifier::Result vr0{};
            runVerify(0, vr0);
            if (vr0.ok) {
                AssistLog::info(fmt::format(
                    "MISS-BAKE-WIN verify-fail→salvage-d0 {} delayWas={} "
                    "pX={:.1f} clickX={:.1f} eF={} lF={}",
                    down ? "press" : "release", delay,
                    playerX, win->x, win->earliest, win->latest
));
                delay = 0;
                vr = vr0;
                if (reuse) reuse->verifyThreat = vr.threatZone;
            }
        }

        if (!vr.ok) {
            char const* why = "verify-fail";
            if (vr.budgetHit) why = "verify-budget";
            else if (vr.diedBeforeAction) why = "verify-died-before";
            else if (!vr.clearsThreat) why = "verify-threat";
            logMissBakeWin(why, win, earliestTick, latestTick, delay, approxFrame);
            if (hybrid && beforeNominal) {
                // Window known, still before replay center — Live must not
                // invent an earlier fire via PassHopeless→HUS (session early assist).
                handled = true;
                this->m_lastBakeHint = fmt::format(
                    "MISS {} → pass (before-nom)", why
);
                AssistLog::info(fmt::format(
                    "MISS-BAKE-WIN {} before-nominal → Pass (no Live/HUS) "
                    "effectT={} nT={} eF={} lF={}",
                    why, effectTick, nominalTick, win->earliest, win->latest
));
                DevCycleRecorder::get().noteGatePass(
                    AssistReason::PassBakeVerifyFail, "verify-before-nominal"
);
                this->noteVerdict(
                    AssistReason::PassBakeVerifyFail, "verify-before-nominal"
);
                this->updateOverlay();
                return true;
            }
            return fallThroughOrPass(why, AssistReason::PassBakeVerifyFail);
        }
    }

    // Template arm — press and release share this path (purple release gated
    // upstream by shouldPlanRelease). No silent PassBakeInWindow.
    char const* armTag = "now";
    if (salvagedPastLatest) armTag = "salvage";
    else if (delay > 0 && effectTick < earliestTick) armTag = "wait-earliest";
    else if (delay > 0) armTag = "wait-nominal";
    char const* armWhere = salvagedPastLatest
        ? "bake-salvage-now"
        : (delay > 0 && effectTick < earliestTick)
            ? "bake-wait-earliest"
            : (delay > 0 ? "bake-wait-nominal" : "bake-now");

    handled = true;
    // Overlay ownership: bake↓/↑ tag window + why (salvage / wait-nom / now).
    this->m_lastBakeHint = fmt::format(
        "bake{} {} [{},{}] +{}",
        down ? "↓" : "↑",
        armTag,
        win->earliest,
        win->latest,
        delay
);

    this->m_buffer = {};
    this->m_buffer.state = BufferState::Buffered;
    this->m_buffer.down = down;
    this->m_buffer.player1 = player1;
    this->m_buffer.pressTick = effectTick;
    this->m_buffer.planDelay = delay;
    this->m_buffer.limit = std::max(limit, std::max(1, delay));
    this->m_buffer.fireTick = resolveFireTick(effectTick, delay, delay, false);
    this->m_buffer.expectedJumpTick = this->m_buffer.fireTick;
    this->m_buffer.decisionX = playerX;
    this->m_buffer.mode = kind;
    this->m_buffer.fromBake = true;

    // Orb heuristic only for presses — releases keep pure template timing.
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
    this->beginShiftQueue(this->m_buffer.fireTick - effectTick);

    AssistLog::info(fmt::format(
        "bake ASSIST {} +{} ticks (window [{},{}] nomF={} "
        "eT={} nT={} lT={} effectT={} fire={} where={} spatial={} approxF={})",
        down ? "press" : "release", delay, win->earliest, win->latest,
        win->nominalFrame, earliestTick, nominalTick, latestTick, effectTick,
        this->m_buffer.fireTick, armWhere, spatialIn ? 1 : 0, approxFrame
));
    {
        DevCycleRecorder::DelayArmMeta meta{};
        meta.armVerifyOk = true;
        meta.budgetHitAtArm = false;
        this->m_buffer.devPressId = DevCycleRecorder::get().noteDelayArm(
            AssistReason::DelayPrefer,
            armWhere,
            delay,
            this->m_buffer.fireTick,
            0,
            DevEventSource::Bake,
            meta
);
    }
    this->noteVerdict(AssistReason::DelayPrefer, armWhere);
    AssistRealMetrics::get().noteArm(kind);
    this->updateOverlay();
    return false;
}

bool AssistController::planPress(
    PlayLayer* playLayer,
    PlayerObject* player,
    bool player1,
    int effectTick,
    float firstDt,
    DevClockContract::Stamp const& clockIn
) {
    DevClockContract::Stamp clock = clockIn;
    int const limit = this->activeLimit(player);
    if (limit <= 0) {
        DevCycleRecorder::get().noteGatePass(AssistReason::PassLimitZero, "press-limit0");
        this->noteVerdict(AssistReason::PassLimitZero, "press-limit0");
        return true;
    }

    // Opportunity: every press that reaches assist decisioning.
    {
        float const xOpp = player->getPositionX();
        bool const trusted = SimTrust::get().okAt(xOpp);
        AssistRealMetrics::get().noteOpportunity(
            trusted, StageConfig::kindOf(player)
);
    }

    // GhostParity / SpotTrust: hard Pass before bake+Live — never soft with orb.
    // Reasons via PerfectAssistPolicy / SimTrust (same tokens).
    {
        float const xGate = player->getPositionX();
        AssistReason parityReason = AssistReason::None;
        if (SimTrust::get().forcePassReason(xGate, parityReason)) {
            AssistLog::debug(fmt::format(
                "SIMTRUST pass press ({})", AssistVerdict::name(parityReason)
));
            this->m_untrustPassHint = true;
            SimPlan dummy;
            dummy.action = PlanAction::Pass;
            dummy.reason = parityReason;
            dummy.firstDeathTick = -1;
            dummy.continuous = player->m_isDart || player->m_isShip || player->m_isSwing;
            char const* where = parityReason == AssistReason::PassParityDrift
                ? "press-parity-drift"
                : "press-spot-untrusted";
            DevCycleRecorder::get().noteGatePass(parityReason, where);
            this->noteVerdict(parityReason, where);
            this->recordDecision(playLayer, player, true, dummy, effectTick, 0, limit);
            this->updateOverlay();
            return true;
        }
    }

    // Hybrid: one press wall/tick envelope for bake verify + Live fallthrough.
    PressSimBudget pressBudget;
    pressBudget.tickLeft = StageConfig::tickBudgetPerPress();
    {
        int const fullUs = StageConfig::simBudgetUs();
        pressBudget.usUnlimited = (fullUs <= 0);
        pressBudget.usLeft = pressBudget.usUnlimited ? 0 : fullUs;
    }
    bool const shareHybrid = StageConfig::assistMode() == AssistMode::Hybrid;

    bool bakeHandled = false;
    bool hybridVerifyAttempted = false;
    HybridPressReuse hybridReuse{};
    bool const bakeRet = this->tryBakeAssist(
        playLayer, player, player1, true, effectTick, firstDt, bakeHandled,
        shareHybrid ? &pressBudget : nullptr,
        shareHybrid ? &hybridVerifyAttempted : nullptr,
        shareHybrid ? &hybridReuse : nullptr
);
    if (bakeHandled) return bakeRet;

    // Times Live fallthrough (verify already paid / skipped). Standalone LagProbe.
    LagProbe::Scope hyLive(
        shareHybrid ? "eb.hybrid.live" : nullptr,
        fmt::format(
            "vfy={} remTick={} remUs={}",
            hybridVerifyAttempted ? 1 : 0,
            pressBudget.tickLeft,
            pressBudget.usUnlimited ? -1 : pressBudget.usLeft
)
);

    auto const kind = StageConfig::kindOf(player);
    float const x = player->getPositionX();
    auto& profile = LevelProfile::get();
    int const slot = player1 ? 0 : 1;
    bool const nearOrb = nearOrbOrPadHint(playLayer, player, limit, this->m_simDt);
    // After Hybrid bake verify rejected the window, do not treat near-bake as
    // spam override — that forced full Live on every ~5 CPS click (main-thread hitch).
    // Reuse findNear result from tryBakeAssist when present (no second lookup).
    bool nearBake = nearBakeWindowHint(
        player, player1, true, effectTick, this->m_simDt,
        shareHybrid ? &hybridReuse : nullptr
);
    if (hybridVerifyAttempted) nearBake = false;

    // Live/Hybrid-live: sim mismatch → hard Pass, unless orb/pad ahead
    // (allow Delay / Soft Hold — solitary early orb miss must still assist).
    if (DivergenceTracker::get().simUntrusted()) {
        if (!nearOrb) {
            AssistLog::debug("UNTRUST pass (press)");
            this->m_untrustPassHint = true;
            SimPlan dummy;
            dummy.action = PlanAction::Pass;
            dummy.reason = AssistReason::PassUntrust;
            dummy.firstDeathTick = -1;
            dummy.continuous = player->m_isDart || player->m_isShip || player->m_isSwing;
            DevCycleRecorder::get().noteGatePass(AssistReason::PassUntrust, "press-untrust");
            this->noteVerdict(AssistReason::PassUntrust, "press-untrust");
            this->recordDecision(playLayer, player, true, dummy, effectTick, 0, limit);
            this->updateOverlay();
            return true;
        }
        AssistLog::debug("UNTRUST soft (press orb foresight — allow plan)");
        this->m_untrustPassHint = false;
    }
    else {
        this->m_untrustPassHint = false;
    }

    // Opportunity + Trust → MUST Delay-search (AssistCov).
    // Named Pass for TW-empty-when-expected (effects baked, 0 tracks).
    bool mustDelaySearch = false;
    {
        PerfectAssistPolicy::TrustInput trustIn;
        trustIn.parityDrift = SimTrust::get().parityDrift();
        trustIn.spotUntrusted = SpotTrustMap::get().isUntrusted(x);
        trustIn.divergenceUntrusted =
            DivergenceTracker::get().simUntrusted() && !nearOrb;
        if (StageConfig::triggerBake()) {
            auto& tw = TriggerWorldStore::get();
            auto& baker = TriggerWorldBaker::get();
            trustIn.twEmptyWhenExpected = tw.loaded()
                && !tw.hasTracks()
                && !baker.running()
                && !baker.lastCollectIncomplete()
                && (baker.lastBakedEffects() > 0 || baker.lastBakedStartTimed() > 0);
        }
        AssistReason namedPass = AssistReason::None;
        if (PerfectAssistPolicy::forcePassReason(trustIn, namedPass)
            && namedPass == AssistReason::PassTwEmpty) {
            AssistLog::debug("PERFECT pass-tw-empty (expected movers, 0 tracks)");
            this->m_untrustPassHint = true;
            SimPlan dummy;
            dummy.action = PlanAction::Pass;
            dummy.reason = AssistReason::PassTwEmpty;
            dummy.firstDeathTick = -1;
            dummy.continuous = player->m_isDart || player->m_isShip || player->m_isSwing;
            DevCycleRecorder::get().noteGatePass(AssistReason::PassTwEmpty, "press-tw-empty");
            this->noteVerdict(AssistReason::PassTwEmpty, "press-tw-empty");
            this->recordDecision(playLayer, player, true, dummy, effectTick, 0, limit);
            this->updateOverlay();
            return true;
        }
        PerfectAssistPolicy::OpportunityInput oppIn;
        oppIn.foresightOrbPad = nearOrb;
        oppIn.bakePreferBand = nearBake;
        oppIn.orbWindowFound = nearOrb;
        auto const decision = PerfectAssistPolicy::classify(trustIn, oppIn);
        mustDelaySearch = (decision == PerfectAssistPolicy::Decision::MustDelaySearch);
        if (mustDelaySearch) {
            AssistLog::debug("PERFECT must-delay-search (trust+opportunity)");
        }
    }

    // Dense / rhythmic spam: ModeAssist + SpamAssistPolicy (feather ≠ PassSpam;
    // trust+opportunity / orb / bake → AllowAssist; mid-queue → drain).
    {
        SpamAssistContext spamCtx;
        spamCtx.kind = kind;
        spamCtx.densityPass = ClickIntentGuard::get().shouldPassThrough(
            slot, effectTick, kind, false, false
        );
        spamCtx.nearOrbOrPad = nearOrb;
        spamCtx.nearBake = nearBake;
        spamCtx.mustDelaySearch = mustDelaySearch;
        spamCtx.bufferActive = false;
        spamCtx.releaseSeen = false;
        spamCtx.shiftQueueActive = this->m_shiftQueue.active();
        spamCtx.queueSize = this->m_shiftQueue.size();
        auto const spamAct = SpamAssistPolicy::decide(spamCtx);
        if (spamAct == SpamAssistAction::DrainSpamPass) {
            this->m_shiftDrainPass = true;
        }
        if (spamAct == SpamAssistAction::PassSpam
            || spamAct == SpamAssistAction::DrainSpamPass
            || spamAct == SpamAssistAction::OverflowPass) {
            AssistReason const why = SpamAssistPolicy::passReason(spamAct);
            AssistLog::debug(fmt::format(
                "intent: {} (mode={}, tick={}, q={})",
                AssistVerdict::name(why),
                StageConfig::kindName(kind),
                effectTick,
                this->m_shiftQueue.size()
));
            SimPlan dummy;
            dummy.action = PlanAction::Pass;
            dummy.reason = why;
            dummy.firstDeathTick = -1;
            dummy.continuous = ModeAssist::forKind(kind).continuous();
            DevCycleRecorder::get().noteGatePass(why, "press-spam");
            this->noteVerdict(why, "press-spam");
            this->recordDecision(playLayer, player, true, dummy, effectTick, 0, limit);
            this->updateOverlay();
            return true;
        }
    }

    // Low trust at this spot → pass through (unless explore roll or orb ahead).
    // Trusted opportunity MustDelaySearch overrides learned low-trust Pass.
    if (!mustDelaySearch
        && StageConfig::autoTune() && profile.loaded() && !nearOrb) {
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
                dummy.reason = AssistReason::PassLowTrust;
                dummy.firstDeathTick = -1;
                DevCycleRecorder::get().noteGatePass(AssistReason::PassLowTrust, "press-low-trust");
                this->noteVerdict(AssistReason::PassLowTrust, "press-low-trust");
                this->recordDecision(playLayer, player, true, dummy, effectTick, 0, limit);
                this->updateOverlay();
                return true;
            }
        }
    }

    PlanOptions opts;
    // Hybrid fallthrough: remainder after bake verify (not a second full envelope).
    // Exhausted remainder (usLeft==0) must NOT map to PlanOptions unlimited.
    if (shareHybrid) {
        opts.tickBudget = std::max(0, pressBudget.tickLeft);
        if (pressBudget.usUnlimited) {
            opts.budgetUs = 0;
        }
        else {
            opts.budgetUs = std::max(1, pressBudget.usLeft);
        }
    }
    else {
        opts.budgetUs = StageConfig::simBudgetUs();
        opts.tickBudget = StageConfig::tickBudgetPerPress();
    }
    // Verify already paid this press: hard-cap Live so verify-fail + full plan
    // cannot hitch the game (~0.75 feel / stutter at moderate CPS).
    if (shareHybrid && hybridVerifyAttempted) {
        int const halfTick = std::max(400, StageConfig::tickBudgetPerPress() / 2);
        opts.tickBudget = std::min(opts.tickBudget, halfTick);
        if (!pressBudget.usUnlimited) {
            int const fullUs = StageConfig::simBudgetUs();
            int const halfUs = std::max(200, fullUs / 2);
            opts.budgetUs = std::min(opts.budgetUs, halfUs);
            opts.budgetUs = std::max(1, opts.budgetUs);
        }
    }
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
    opts.legacyPick = StageConfig::assistPickLegacy();
    opts.assistEngine = StageConfig::effectiveAssistEngine();
    opts.mustDelaySearch = mustDelaySearch;
    // Live Delay-arm: only Alive ghosts (bake verify keeps soft Landing/LateCrash).
    opts.requireAliveForDelay = true;
    // Density without orb/bake override — softens Wave λ / bumps auto k when
    // we still assist (e.g. orb in spam corridor).
    bool const spamDense = ClickIntentGuard::get().shouldPassThrough(
        slot, effectTick, kind
);
    fillPlanRobustLatency(opts, player, this->m_dtVarEma, spamDense);
    // Rejected bake window must not steer Live prefer-band (wasted sims / hitch).
    // Otherwise reuse findNear window fields from tryBakeAssist.
    if (!hybridVerifyAttempted) {
        fillBakePreferBand(
            opts, player, player1, true, effectTick, limit, this->m_attemptTick, this->m_simDt,
            shareHybrid ? &hybridReuse : nullptr
);
    }
    // Verify-fail: seed threat zone so plan() skips buildThreatZone.
    if (hybridVerifyAttempted && hybridReuse.verifyThreat.valid) {
        opts.seedThreatZone = hybridReuse.verifyThreat;
    }

    auto const t0 = std::chrono::steady_clock::now();
    // planSequence only when Smart Depth > 1 (New depth-1 exit-predict removed).
    // After Hybrid verify, skip when wall remainder is tight.
    bool useSequence = opts.smartDepth > 1;
    if (hybridVerifyAttempted && !pressBudget.usUnlimited && opts.budgetUs < 600) {
        useSequence = false;
    }
    AssistSpikeWatch::get().beginSearch("press");
    auto plan = useSequence
        ? ShadowSimulator::get().planSequence(playLayer, player, true, limit, m_simDt, firstDt, opts)
        : ShadowSimulator::get().plan(playLayer, player, true, limit, m_simDt, firstDt, opts);
    int const planUs = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0
).count());
    AssistSpikeWatch::get().endSearch(planUs, plan.budgetHit);
    AssistPerfStats::get().notePlan(planUs, plan.budgetHit);

    this->logPlan(plan, "press");
    this->m_outcomePlayer1 = player1;
    this->beginOutcome(plan, effectTick);
    clock.liveSimPathId = useSequence
        ? DevSimPathId::PlanSequence
        : DevSimPathId::Plan;
    clock.effectTick = effectTick;
    DevCycleRecorder::get().notePlanFinished(
        plan, effectTick, limit, clock, playLayer, player
);

    // Belt: never arm Delay when sim marked unknown / toggle-target uncertainty.
    if (plan.action == PlanAction::Delay && (plan.unknown || plan.toggleUnknown)) {
        AssistLog::debug(fmt::format(
            "press: refuse Delay (toggle/unknown) → Pass id={}",
            plan.planId
));
        plan.action = PlanAction::Pass;
        plan.delay = 0;
        plan.reason = AssistReason::PassToggleUnknown;
        plan.unknown = true;
        plan.toggleUnknown = true;
    }

    // Do not invent Delay without a viable cand (plan already gated).
    // budgetHit after a full pick is OK — Soft Hold covers PassBudget separately.

    if (plan.action != PlanAction::Delay) {
        bool const trustOk = SimTrust::get().okAt(x);
        if (StageConfig::husSoftHoldEnabled()
            && shouldArmHoldUntilSafe(plan, trustOk)) {
            // Hybrid: bake window still ahead of nominal → no HUS mid-window.
            int nomTick = -1;
            if (shareHybrid
                && husBlockedBeforeBakeNominal(
                    hybridReuse, effectTick, this->m_simDt, nomTick
)) {
                AssistLog::info(fmt::format(
                    "MISS-BAKE-WIN hus-suppressed-before-nominal press "
                    "effectT={} nomT={} eF={} lF={} — Pass (no early HUS)",
                    effectTick, nomTick,
                    hybridReuse.earliest, hybridReuse.latest
));
                this->m_lastBakeHint = "MISS hus→before-nom → pass";
                DevCycleRecorder::get().noteGatePass(
                    AssistReason::PassHopeless, "hus-before-bake-nominal"
);
                this->noteVerdict(AssistReason::PassHopeless, "hus-before-bake-nominal");
                this->recordDecision(
                    playLayer, player, true, plan, effectTick, 0, false
);
                this->updateOverlay();
                return true;
            }
            return this->armHoldUntilSafe(
                playLayer, player, player1, effectTick, limit, plan
);
        }
        if (plan.longForesightBothDead) {
            AssistLog::info(fmt::format(
                "ASSIST press LF both-dead id={} — Pass (no escape force)",
                plan.planId
));
        }
        this->noteNoEscapeHonesty(plan, "press-plan");
        AssistReason passReason = plan.reason;
        if (passReason == AssistReason::None) {
            passReason = AssistReason::PassUnknown;
        }
        DevCycleRecorder::get().notePassThrough(passReason, "press-plan");
        this->noteVerdict(passReason, "press-plan");
        this->recordDecision(playLayer, player, true, plan, effectTick, 0, limit);
        // Vanilla/Pass click timing — feed earliness histogram (never inject saves).
        if (StageConfig::autoTune() && profile.loaded()) {
            profile.noteEarliness(kind, x, 0, "pass");
        }
        this->updateOverlay();
        return true;
    }

    int delay = plan.delay;
    int const plannedDelay = plan.delay;
    bool learningFloored = false;
    bool learningNudged = false;
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
                // Dev recording + disable-random: keep live/eval deterministic.
                bool const freezeRand = DevConfig::recordEnable()
                    && DevConfig::disableRandom()
                    && DevCycleRecorder::get().isRecording();
                if (!freezeRand) {
                    off += (std::rand() % 3) - 1;
                }
                AssistLog::debug(fmt::format(
                    "learn: hist explore ±1 @{} histN={}",
                    LevelProfile::spotBucket(x), spot->histN
));
            }
            // After assist deaths at this spot, nudge later — never pull earlier.
            if (spot->diedAfterAssist > 0 && off < 1) {
                off = 1;
            }
            delay = plannedDelay + off;
        }
        // Miss-assist: learning must not undo the planner wait (caused +N then die).
        if (delay < plannedDelay) {
            AssistLog::debug(fmt::format(
                "learn: floor delay {}→{} (never earlier than plan)",
                delay, plannedDelay
));
            delay = plannedDelay;
            learningFloored = true;
        }
        // Clamp bias into activation window when requireOrb
        if (plan.requireOrb && plan.dMin >= 0 && plan.dMax >= plan.dMin) {
            delay = std::max(plan.dMin, std::min(plan.dMax, delay));
            // Still never earlier than plan when plan was already in-window.
            if (plannedDelay >= plan.dMin && delay < plannedDelay) {
                delay = plannedDelay;
                learningFloored = true;
            }
        }
        delay = MissAssistContracts::clampDelayToWindow(delay, limit);
        if (delay < 1) delay = 1;
        learningNudged = (delay != plannedDelay) && !learningFloored;
        if (learningNudged || learningFloored) {
            DevCycleRecorder::get().noteLearningOnPlan(
                plannedDelay, delay, learningFloored, false
);
        }
    }

    // If auto-tune shifted delay away from plannedDelay, verify the shifted delay;
    // when plannedDelay is kept as-is, ShadowSimulator::plan already verified it.
    bool armVerifyOk = true;
    if (delay > 0 && delay != plannedDelay && this->m_simDt > 0.f) {
        float const fd = (firstDt > 0.f && firstDt <= this->m_simDt) ? firstDt : this->m_simDt;
        PlanOptions vopts = opts;
        vopts.tickBudget = std::max(200, StageConfig::tickBudgetPerPress() / 4);
        vopts.budgetUs = StageConfig::husReplanBudgetUs();
        vopts.maxBranches = 0;
        vopts.smartDepth = 1;
        auto const vr = ShadowSimulator::get().verifyDelay(
            playLayer, player, true, delay, limit, this->m_simDt, fd, vopts
);
        if (!vr.ok) {
            AssistLog::debug(fmt::format(
                "ASSIST learning shift {}→{} verify-fail → keep plannedDelay={}",
                plannedDelay, delay, plannedDelay
));
            delay = plannedDelay;
        }
        armVerifyOk = vr.ok;
    }

    // Wrong-orb reject — activation outside intended window.
    if (plan.requireOrb
        && OrbAlign::wrongOrbReject(delay, plan.dMin, plan.dMax)) {
        AssistLog::debug(fmt::format(
            "ASSIST wrong-orb reject delay={} win=[{},{}] → Pass",
            delay, plan.dMin, plan.dMax
));
        SimPlan pass = plan;
        pass.action = PlanAction::Pass;
        pass.delay = 0;
        pass.reason = AssistReason::PassUnknown;
        bool const trustOk = SimTrust::get().okAt(x);
        if (StageConfig::husSoftHoldEnabled()
            && shouldArmHoldUntilSafe(pass, trustOk)) {
            return this->armHoldUntilSafe(
                playLayer, player, player1, effectTick, limit, pass
);
        }
        DevCycleRecorder::get().notePassThrough(pass.reason, "press-wrong-orb");
        this->noteVerdict(pass.reason, "press-wrong-orb");
        this->recordDecision(playLayer, player, true, pass, effectTick, 0, limit);
        this->updateOverlay();
        return true;
    }

    // Robust ±1 vor Arm — center + existing neighbors must clear.
    if (delay > 0 && this->m_simDt > 0.f) {
        float const fd = (firstDt > 0.f && firstDt <= this->m_simDt) ? firstDt : this->m_simDt;
        PlanOptions vopts = opts;
        vopts.tickBudget = std::max(200, StageConfig::tickBudgetPerPress() / 4);
        vopts.budgetUs = StageConfig::husReplanBudgetUs();
        vopts.maxBranches = 0;
        vopts.smartDepth = 1;
        auto verifyOne = [&](int d) -> bool {
            if (d < 1 || d > limit) return true;
            return ShadowSimulator::get().verifyDelay(
                playLayer, player, true, d, limit, this->m_simDt, fd, vopts
).ok;
        };
        bool const centerOk = verifyOne(delay);
        bool const hasLo = delay - 1 >= 1;
        bool const hasHi = delay + 1 <= limit;
        bool const loOk = !hasLo || verifyOne(delay - 1);
        bool const hiOk = !hasHi || verifyOne(delay + 1);
        if (!OrbAlign::robustNeighborsOk(centerOk, hasLo, loOk, hasHi, hiOk)) {
            AssistLog::debug(fmt::format(
                "ASSIST robust±1 fail delay={} c={} lo={} hi={} → Pass/HUS",
                delay, centerOk ? 1 : 0, loOk ? 1 : 0, hiOk ? 1 : 0
));
            armVerifyOk = false;
            SimPlan pass = plan;
            pass.action = PlanAction::Pass;
            pass.delay = 0;
            pass.reason = AssistReason::PassHopeless;
            bool const trustOk = SimTrust::get().okAt(x);
            // Robust refuse: HUS only with orb/Alive escape (not bare hopeless).
            if (StageConfig::husSoftHoldEnabled()
                && shouldArmHoldUntilSafe(pass, trustOk)) {
                return this->armHoldUntilSafe(
                    playLayer, player, player1, effectTick, limit, pass
);
            }
            this->noteNoEscapeHonesty(pass, "press-robust-fail");
            DevCycleRecorder::get().notePassThrough(pass.reason, "press-robust-fail");
            this->noteVerdict(pass.reason, "press-robust-fail");
            this->recordDecision(playLayer, player, true, pass, effectTick, 0, limit);
            this->updateOverlay();
            return true;
        }
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
    this->m_buffer.releaseAfter = plan.releaseAfter;
    this->m_buffer.releaseEarliest = plan.releaseEarliest;
    this->m_buffer.releaseLatest = plan.releaseLatest;
    // Orb align: early press waits inside ring window.
    if (plan.requireOrb) {
        this->m_buffer.dMin = plan.dMin;
        this->m_buffer.dMax = plan.dMax;
        this->m_buffer.fireTick = OrbAlign::clampFireIntoOrbWindow(
            this->m_buffer.fireTick,
            effectTick,
            plan.dMin,
            plan.dMax,
            limit
);
        this->m_buffer.fireTick = OrbAlign::waitUntilInsideRing(
            this->m_buffer.fireTick, effectTick, plan.dMin
);
    }
    else {
        this->m_buffer.dMin = -1;
        this->m_buffer.dMax = -1;
    }
    // Never arm past the forgiveness deadline.
    this->m_buffer.fireTick = std::min(this->m_buffer.fireTick, effectTick + limit);
    // Non-orb: never fire earlier than planned delay (guards incidental clamps).
    if (!plan.requireOrb && this->m_buffer.fireTick < effectTick + delay) {
        this->m_buffer.fireTick = effectTick + delay;
        this->m_buffer.fireTick = std::min(
            this->m_buffer.fireTick, effectTick + limit
);
    }
    this->beginShiftQueue(this->m_buffer.fireTick - effectTick);

    AssistLog::debug(fmt::format(
        "ASSIST press +{} ticks id={} ring={} hold={} rel=[{},{}]@{} win=[{},{}] fire={} jump={} orb={}",
        delay, plan.planId, plan.activatedRing, plan.requireHold,
        plan.releaseEarliest, plan.releaseLatest, plan.releaseAfter,
        this->m_buffer.dMin, this->m_buffer.dMax,
        this->m_buffer.fireTick, this->m_buffer.expectedJumpTick,
        plan.requireOrb ? 1 : 0
));
    AssistReason delayReason = plan.reason;
    if (!AssistVerdict::isDelay(delayReason)) {
        delayReason = AssistVerdict::inferDelayReason(
            plan.activatedRing, plan.requireOrb,
            opts.preferDelayMin >= 0 && opts.preferDelayMax >= opts.preferDelayMin
);
    }
    {
        DevCycleRecorder::DelayArmMeta meta{};
        meta.armVerifyOk = armVerifyOk;
        meta.budgetHitAtArm = plan.budgetHit;
        this->m_buffer.devPressId = DevCycleRecorder::get().noteDelayArm(
            delayReason,
            "press-delay",
            delay,
            this->m_buffer.fireTick,
            plan.planId,
            DevEventSource::Live,
            meta
);
    }
    this->noteVerdict(delayReason, "press-delay");
    if (delay > 0) {
        AssistRealMetrics::get().noteArm(StageConfig::kindOf(player));
    }
    this->recordDecision(playLayer, player, true, plan, effectTick, delay, limit);
    this->updateOverlay();
    return false;
}

bool AssistController::planRelease(
    PlayLayer* playLayer,
    PlayerObject* player,
    bool player1,
    int effectTick,
    float firstDt,
    DevClockContract::Stamp const& clockIn
) {
    DevClockContract::Stamp clock = clockIn;

    // Same rules as purple bake viz: wave/ship/swing/robot, else dash/holdAssist only.
    // Cube/ball/ufo/spider normal UP → Pass (no false release ASSIST).
    if (!this->shouldPlanRelease(playLayer, player, player1, effectTick)) {
        AssistLog::debug(fmt::format(
            "release: discrete-skip mode={} dashing={} holdAssistBake=0",
            StageConfig::kindName(StageConfig::kindOf(player)),
            player && player->m_isDashing ? 1 : 0
));
        DevCycleRecorder::get().noteGatePass(
            AssistReason::PassReleaseDiscrete, "release-discrete-skip"
);
        this->noteVerdict(AssistReason::PassReleaseDiscrete, "release-discrete-skip");
        this->updateOverlay();
        return true;
    }

    int const limit = this->activeLimit(player);
    if (limit <= 0) {
        DevCycleRecorder::get().noteGatePass(AssistReason::PassLimitZero, "release-limit0");
        this->noteVerdict(AssistReason::PassLimitZero, "release-limit0");
        return true;
    }

    // GhostParity / SpotTrust: hard Pass before bake (same honesty as press).
    {
        float const xGate = player->getPositionX();
        AssistReason parityReason = AssistReason::None;
        if (SimTrust::get().forcePassReason(xGate, parityReason)) {
            AssistLog::debug(fmt::format(
                "SIMTRUST pass release ({})", AssistVerdict::name(parityReason)
));
            this->m_untrustPassHint = true;
            char const* where = parityReason == AssistReason::PassParityDrift
                ? "release-parity-drift"
                : "release-spot-untrusted";
            DevCycleRecorder::get().noteGatePass(parityReason, where);
            this->noteVerdict(parityReason, where);
            this->updateOverlay();
            return true;
        }
    }

    PressSimBudget pressBudget;
    pressBudget.tickLeft = StageConfig::tickBudgetPerPress();
    {
        int const fullUs = StageConfig::simBudgetUs();
        pressBudget.usUnlimited = (fullUs <= 0);
        pressBudget.usLeft = pressBudget.usUnlimited ? 0 : fullUs;
    }
    bool const shareHybrid = StageConfig::assistMode() == AssistMode::Hybrid;

    bool bakeHandled = false;
    bool hybridVerifyAttempted = false;
    HybridPressReuse hybridReuse{};
    bool const bakeRet = this->tryBakeAssist(
        playLayer, player, player1, false, effectTick, firstDt, bakeHandled,
        shareHybrid ? &pressBudget : nullptr,
        shareHybrid ? &hybridVerifyAttempted : nullptr,
        shareHybrid ? &hybridReuse : nullptr
);
    if (bakeHandled) return bakeRet;

    LagProbe::Scope hyLive(
        shareHybrid ? "eb.hybrid.live" : nullptr,
        fmt::format(
            "vfy={} remTick={} remUs={}",
            hybridVerifyAttempted ? 1 : 0,
            pressBudget.tickLeft,
            pressBudget.usUnlimited ? -1 : pressBudget.usLeft
)
);

    auto const kind = StageConfig::kindOf(player);
    float const x = player->getPositionX();
    auto& profile = LevelProfile::get();
    int const slot = player1 ? 0 : 1;
    bool const nearOrb = nearOrbOrPadHint(playLayer, player, limit, this->m_simDt);
    bool nearBake = nearBakeWindowHint(
        player, player1, false, effectTick, this->m_simDt,
        shareHybrid ? &hybridReuse : nullptr
);
    if (hybridVerifyAttempted) nearBake = false;

    if (DivergenceTracker::get().simUntrusted()) {
        if (!nearOrb) {
            AssistLog::debug("UNTRUST pass (release)");
            this->m_untrustPassHint = true;
            DevCycleRecorder::get().noteGatePass(AssistReason::PassUntrust, "release-untrust");
            this->noteVerdict(AssistReason::PassUntrust, "release-untrust");
            this->updateOverlay();
            return true;
        }
        AssistLog::debug("UNTRUST soft (release orb foresight — allow plan)");
        this->m_untrustPassHint = false;
    }
    else {
        this->m_untrustPassHint = false;
    }

    // Same PerfectAssist as press — trust+opportunity must Delay-search
    // (wave hold-longer saves must not lose to spam / PassNoThreat).
    bool mustDelaySearch = false;
    {
        PerfectAssistPolicy::TrustInput trustIn;
        trustIn.parityDrift = SimTrust::get().parityDrift();
        trustIn.spotUntrusted = SpotTrustMap::get().isUntrusted(x);
        trustIn.divergenceUntrusted =
            DivergenceTracker::get().simUntrusted() && !nearOrb;
        if (StageConfig::triggerBake()) {
            auto& tw = TriggerWorldStore::get();
            auto& baker = TriggerWorldBaker::get();
            trustIn.twEmptyWhenExpected = tw.loaded()
                && !tw.hasTracks()
                && !baker.running()
                && !baker.lastCollectIncomplete()
                && (baker.lastBakedEffects() > 0 || baker.lastBakedStartTimed() > 0);
        }
        AssistReason namedPass = AssistReason::None;
        if (PerfectAssistPolicy::forcePassReason(trustIn, namedPass)
            && namedPass == AssistReason::PassTwEmpty) {
            AssistLog::debug("PERFECT pass-tw-empty (release)");
            this->m_untrustPassHint = true;
            DevCycleRecorder::get().noteGatePass(AssistReason::PassTwEmpty, "release-tw-empty");
            this->noteVerdict(AssistReason::PassTwEmpty, "release-tw-empty");
            this->updateOverlay();
            return true;
        }
        PerfectAssistPolicy::OpportunityInput oppIn;
        oppIn.foresightOrbPad = nearOrb;
        oppIn.bakePreferBand = nearBake;
        oppIn.orbWindowFound = nearOrb;
        auto const decision = PerfectAssistPolicy::classify(trustIn, oppIn);
        mustDelaySearch = (decision == PerfectAssistPolicy::Decision::MustDelaySearch);
        if (mustDelaySearch) {
            AssistLog::debug("PERFECT must-delay-search release (trust+opportunity)");
        }
    }

    // Continuous spam: do not delay releases (would steal hold rhythm) unless
    // PerfectAssist / feather / orb demands assist (SpamAssistPolicy).
    if (ModeAssist::forKind(kind).continuous()) {
        SpamAssistContext spamCtx;
        spamCtx.kind = kind;
        spamCtx.densityPass = ClickIntentGuard::get().shouldPassThrough(
            slot, effectTick, kind, false, false
        );
        spamCtx.nearOrbOrPad = nearOrb;
        spamCtx.nearBake = nearBake;
        spamCtx.mustDelaySearch = mustDelaySearch;
        spamCtx.shiftQueueActive = this->m_shiftQueue.active();
        spamCtx.queueSize = this->m_shiftQueue.size();
        auto const spamAct = SpamAssistPolicy::decide(spamCtx);
        if (spamAct == SpamAssistAction::DrainSpamPass) {
            this->m_shiftDrainPass = true;
        }
        if (spamAct == SpamAssistAction::PassSpam
            || spamAct == SpamAssistAction::DrainSpamPass
            || spamAct == SpamAssistAction::OverflowPass) {
            AssistReason const why = SpamAssistPolicy::passReason(spamAct);
            AssistLog::debug(fmt::format(
                "intent: {} release (mode={}, tick={}, q={})",
                AssistVerdict::name(why),
                StageConfig::kindName(kind),
                effectTick,
                this->m_shiftQueue.size()
));
            DevCycleRecorder::get().noteGatePass(why, "release-spam");
            this->noteVerdict(why, "release-spam");
            this->updateOverlay();
            return true;
        }
    }

    if (!mustDelaySearch
        && StageConfig::autoTune() && profile.loaded() && !nearOrb) {
        if (auto const* spot = profile.trySpot(kind, x)) {
            if (spot->n >= LevelProfile::TRUST_PASS_MIN_N
                && spot->trust < LevelProfile::TRUST_PASS_THRESHOLD
                && !AssistLearning::shouldExploreDespiteLowTrust(spot->trust)) {
                AssistLog::debug(fmt::format(
                    "learned: low trust @{} (release)", LevelProfile::spotBucket(x)
));
                AssistLearning::decaySpot(kind, x);
                DevCycleRecorder::get().noteGatePass(AssistReason::PassLowTrust, "release-low-trust");
                this->noteVerdict(AssistReason::PassLowTrust, "release-low-trust");
                this->updateOverlay();
                return true;
            }
        }
    }

    PlanOptions opts;
    if (shareHybrid) {
        opts.tickBudget = std::max(0, pressBudget.tickLeft);
        if (pressBudget.usUnlimited) {
            opts.budgetUs = 0;
        }
        else {
            opts.budgetUs = std::max(1, pressBudget.usLeft);
        }
    }
    else {
        opts.budgetUs = StageConfig::simBudgetUs();
        opts.tickBudget = StageConfig::tickBudgetPerPress();
    }
    if (shareHybrid && hybridVerifyAttempted) {
        int const halfTick = std::max(400, StageConfig::tickBudgetPerPress() / 2);
        opts.tickBudget = std::min(opts.tickBudget, halfTick);
        if (!pressBudget.usUnlimited) {
            int const fullUs = StageConfig::simBudgetUs();
            int const halfUs = std::max(200, fullUs / 2);
            opts.budgetUs = std::max(1, std::min(opts.budgetUs, halfUs));
        }
    }
    opts.capMul = StageConfig::autoTune() ? profile.capMul(kind) : 1.f;
    if (DivergenceTracker::get().shouldWarn()) {
        opts.capMul *= 0.85f;
    }
    opts.maxBranches = 4;
    opts.attemptStartTick = effectTick;
    opts.legacyPick = StageConfig::assistPickLegacy();
    opts.assistEngine = StageConfig::effectiveAssistEngine();
    opts.mustDelaySearch = mustDelaySearch;
    // Live Delay-arm: only Alive ghosts (bake verify keeps soft Landing/LateCrash).
    opts.requireAliveForDelay = true;
    bool const spamDenseRel = ClickIntentGuard::get().shouldPassThrough(
        slot, effectTick, kind
);
    fillPlanRobustLatency(opts, player, this->m_dtVarEma, spamDenseRel);
    if (!hybridVerifyAttempted) {
        fillBakePreferBand(
            opts, player, player1, false, effectTick, limit, this->m_attemptTick, this->m_simDt,
            shareHybrid ? &hybridReuse : nullptr
);
    }
    if (hybridVerifyAttempted && hybridReuse.verifyThreat.valid) {
        opts.seedThreatZone = hybridReuse.verifyThreat;
    }

    AssistSpikeWatch::get().beginSearch("release");
    auto const relT0 = std::chrono::steady_clock::now();
    auto plan = ShadowSimulator::get().plan(playLayer, player, false, limit, m_simDt, firstDt, opts);
    int const relUs = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - relT0
).count());
    AssistSpikeWatch::get().endSearch(relUs, plan.budgetHit);
    AssistPerfStats::get().notePlan(relUs, plan.budgetHit);
    clock.liveSimPathId = DevSimPathId::Plan;
    clock.effectTick = effectTick;
    DevCycleRecorder::get().notePlanFinished(
        plan, effectTick, limit, clock, playLayer, player
);

    // Belt: never arm Delay when sim marked unknown / toggle-target uncertainty.
    if (plan.action == PlanAction::Delay && (plan.unknown || plan.toggleUnknown)) {
        AssistLog::debug(fmt::format(
            "release: refuse Delay (toggle/unknown) → Pass id={}",
            plan.planId
));
        plan.action = PlanAction::Pass;
        plan.delay = 0;
        plan.reason = AssistReason::PassToggleUnknown;
        plan.unknown = true;
        plan.toggleUnknown = true;
    }

    if (plan.action != PlanAction::Delay) {
        bool const trustOk = SimTrust::get().okAt(x);
        // Soft Hold on release: keep Jump held, replan until Delay UP or deadline.
        // Same before-nominal HUS block as press (release template parity).
        if (StageConfig::husSoftHoldEnabled()
            && shouldArmHoldUntilSafe(plan, trustOk)) {
            int nomTick = -1;
            if (shareHybrid
                && husBlockedBeforeBakeNominal(
                    hybridReuse, effectTick, this->m_simDt, nomTick
)) {
                AssistLog::info(fmt::format(
                    "MISS-BAKE-WIN hus-suppressed-before-nominal release "
                    "effectT={} nomT={} eF={} lF={} — Pass (no early HUS)",
                    effectTick, nomTick,
                    hybridReuse.earliest, hybridReuse.latest
));
                this->m_lastBakeHint = "MISS hus→before-nom → pass";
                DevCycleRecorder::get().noteGatePass(
                    AssistReason::PassHopeless, "hus-before-bake-nominal"
);
                this->noteVerdict(AssistReason::PassHopeless, "hus-before-bake-nominal");
                this->recordDecision(
                    playLayer, player, false, plan, effectTick, 0, false
);
                this->updateOverlay();
                return true;
            }
            return this->armHoldUntilSafe(
                playLayer, player, player1, effectTick, limit, plan, false
);
        }
        if (plan.longForesightBothDead) {
            AssistLog::info(fmt::format(
                "ASSIST release LF both-dead id={} — Pass (no escape force)",
                plan.planId
));
        }
        this->noteNoEscapeHonesty(plan, "release-plan");
        AssistReason passReason = plan.reason;
        if (passReason == AssistReason::None) {
            passReason = AssistReason::PassUnknown;
        }
        DevCycleRecorder::get().notePassThrough(passReason, "release-plan");
        this->noteVerdict(passReason, "release-plan");
        this->recordDecision(playLayer, player, false, plan, effectTick, 0, limit);
        this->updateOverlay();
        return true;
    }

    int delay = plan.delay;
    int const plannedDelay = plan.delay;
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
                bool const freezeRand = DevConfig::recordEnable()
                    && DevConfig::disableRandom()
                    && DevCycleRecorder::get().isRecording();
                if (!freezeRand) {
                    off += (std::rand() % 3) - 1;
                }
                AssistLog::debug(fmt::format(
                    "learn: hist explore ±1 release @{} histN={}",
                    LevelProfile::spotBucket(x), spot->histN
));
            }
            if (spot->diedAfterAssist > 0 && off < 1) {
                off = 1;
            }
            delay = plannedDelay + off;
        }
        if (delay < plannedDelay) {
            delay = plannedDelay;
        }
        delay = MissAssistContracts::clampDelayToWindow(delay, limit);
        if (delay < 1) delay = 1;
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
    this->beginShiftQueue(this->m_buffer.fireTick - effectTick);

    AssistLog::debug(fmt::format(
        "ASSIST release +{} ticks (now dies @+{}, limit {}, tick={} fire={})",
        delay, plan.firstDeathTick, limit, effectTick, this->m_buffer.fireTick
));
    AssistReason delayReason = plan.reason;
    if (!AssistVerdict::isDelay(delayReason)) {
        delayReason = AssistVerdict::inferDelayReason(
            plan.activatedRing, plan.requireOrb,
            opts.preferDelayMin >= 0 && opts.preferDelayMax >= opts.preferDelayMin
);
    }
    {
        DevCycleRecorder::DelayArmMeta meta{};
        meta.armVerifyOk = true;
        meta.budgetHitAtArm = plan.budgetHit;
        this->m_buffer.devPressId = DevCycleRecorder::get().noteDelayArm(
            delayReason,
            "release-delay",
            delay,
            this->m_buffer.fireTick,
            plan.planId,
            DevEventSource::Live,
            meta
);
    }
    this->noteVerdict(delayReason, "release-delay");
    if (delay > 0) {
        AssistRealMetrics::get().noteArm(StageConfig::kindOf(player));
    }
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
        this->m_pendingReleaseUserUpTick = -1;
        this->m_pendingReleaseMaxExtend = 0;
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

    int const playerIndex = player1 ? 0 : 1;
    auto beginDevPress = [&]() {
        DevCycleRecorder::get().beginPress(
            playerIndex, down, effectTick, this->m_attemptTick, DevEventSource::Live
);
    };

    // Min click interval: skip assist only — never block the vanilla click.
    // While shift-queue / Delay buffer is live, interval must not block enqueue.
    bool const shiftLive =
        this->m_shiftQueue.active()
        || this->m_buffer.state == BufferState::Buffered;
    if (down) {
        int const minMs = StageConfig::minClickIntervalMs();
        if (minMs > 0
            && !this->m_clickTimes.empty()
            && SpamAssistPolicy::debounceBlocksAssist(shiftLive)) {
            auto const elapsed = std::chrono::steady_clock::now() - this->m_clickTimes.back();
            if (elapsed < std::chrono::milliseconds(minMs)) {
                AssistLog::debug(fmt::format(
                    "click debounce: no assist ({}ms < {}ms), pass through",
                    std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
                    minMs
));
                beginDevPress();
                DevCycleRecorder::get().noteGatePass(AssistReason::PassDebounce, "debounce");
                this->noteVerdict(AssistReason::PassDebounce, "debounce");
                return finish(true);
            }
        }
        this->noteUserClick();
        ClickIntentGuard::get().notePress(
            playerIndex, effectTick, StageConfig::kindOf(player)
);
    }

    // firstDt: full sim dt between frames; CBF remainder / timeWarp when mid-step.
    // Never fold the remainder into effectTick (that caused ±1 vs between-frames).
    float tw = playLayer->m_gameState.m_timeWarp;
    if (tw <= 0.f) tw = 1.f;
    float firstDt = this->m_simDt;
    if (midStep) {
        float rem = this->m_lastDt - this->m_subDtSeen;
        float const minDt = this->m_lastDt * 0.02f;
        if (rem < minDt) rem = minDt;
        if (rem > this->m_lastDt) rem = this->m_lastDt;
        firstDt = rem / tw;
        if (firstDt > this->m_simDt) firstDt = this->m_simDt;
    }
    DevClockContract::Stamp const clockStamp = DevClockContract::get().makeStamp(
        this->m_attemptTick,
        midStep,
        continuous,
        this->m_simDt,
        firstDt,
        this->m_lastDt,
        tw,
        DevSimPathId::None
);

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
            // Hold length under Delay: only pair UP into the queue when a
            // follow-up DOWN was already enqueued (primary uses releaseSeen).
            if (this->m_shiftQueue.active() && this->m_shiftQueue.size() > 0) {
                if (!this->m_shiftQueue.enqueue(false, player1, effectTick)) {
                    beginDevPress();
                    DevCycleRecorder::get().noteGatePass(
                        AssistReason::PassQueueOverflow, "queue-overflow-up"
                    );
                    this->noteVerdict(AssistReason::PassQueueOverflow, "queue-overflow-up");
                    this->updateOverlay();
                    return finish(true);
                }
            }
            this->m_buffer.releaseSeen = true;
            this->m_buffer.releaseTick = effectTick;
            this->m_log.noteInput(effectTick, false);
            // Early UP does not cancel Delay: stay Buffered, fire still due.
            AssistLog::debug(fmt::format(
                "buffer: early UP → releaseSeen=1 stay Buffered fireTick={} attempt={} q={} p{}",
                this->m_buffer.fireTick,
                this->m_attemptTick,
                this->m_shiftQueue.size(),
                player1 ? 1 : 2
));
            this->updateOverlay();
            return false;
        }

        // Extend-hold: user let go — swallow so Jump can stay down ≤ maxExtend ticks.
        if (this->m_pendingRelease && this->m_pendingReleaseP1 == player1) {
            if (this->m_pendingReleaseUserUpTick < 0) {
                this->m_pendingReleaseUserUpTick = this->m_attemptTick;
                this->m_pendingReleaseTick =
                    this->m_pendingReleaseUserUpTick + this->m_pendingReleaseMaxExtend;
            }
            AssistLog::debug(fmt::format(
                "hold-assist: swallow user release @{} extend≤{} deadline={}",
                this->m_attemptTick,
                this->m_pendingReleaseMaxExtend,
                this->m_pendingReleaseTick
));
            return false;
        }

        if (!this->shouldAssistPlayer(player) || !StageConfig::assistRelease()) {
            beginDevPress();
            if (this->cpsLimited()) {
                DevCycleRecorder::get().noteGatePass(AssistReason::PassCps, "release-cps");
                this->noteVerdict(AssistReason::PassCps, "release-cps");
            }
            else if (AssistScopeGate::blockReason(playLayer, player)) {
                DevCycleRecorder::get().noteGatePass(AssistReason::PassScope, "release-scope");
                this->noteVerdict(AssistReason::PassScope, "release-scope");
            }
            else {
                DevCycleRecorder::get().noteGatePass(AssistReason::PassAssistOff, "release-off");
                this->noteVerdict(AssistReason::PassAssistOff, "release-off");
            }
            return finish(true);
        }
        if (this->m_attemptTick <= 2) {
            beginDevPress();
            DevCycleRecorder::get().noteGatePass(AssistReason::PassEarlyAttempt, "release-early");
            this->noteVerdict(AssistReason::PassEarlyAttempt, "release-early");
            return finish(true);
        }
        beginDevPress();
        bool const pass = this->planRelease(
            playLayer, player, player1, effectTick, firstDt, clockStamp
);
        if (pass) this->m_log.noteInput(effectTick, false);
        return finish(pass);
    }

    if (ours && this->m_buffer.state == BufferState::Buffered && this->m_buffer.down) {
        // Follow-up DOWN while A armed:
        //  - releaseSeen (classic re-click): fire A, then plan B (Live Delay path)
        //  - else: same-shift enqueue (multi-key / held spam rhythm)
        if (this->m_shiftQueue.active() && !this->m_buffer.releaseSeen) {
            SpamAssistContext spamCtx;
            spamCtx.kind = StageConfig::kindOf(player);
            spamCtx.bufferActive = true;
            spamCtx.shiftQueueActive = true;
            spamCtx.releaseSeen = false;
            spamCtx.queueSize = this->m_shiftQueue.size();
            auto const act = SpamAssistPolicy::decide(spamCtx);
            if (act == SpamAssistAction::EnqueueShifted
                && this->m_shiftQueue.enqueue(true, player1, effectTick)) {
                AssistLog::debug(fmt::format(
                    "shift-queue: enqueue DOWN tick={} fire={} q={} shift={}",
                    effectTick,
                    effectTick + this->m_shiftQueue.shift(),
                    this->m_shiftQueue.size(),
                    this->m_shiftQueue.shift()
));
                this->updateOverlay();
                return false;
            }
            if (act == SpamAssistAction::OverflowPass
                || this->m_shiftQueue.size() >= ShiftedInputQueue::kCap) {
                beginDevPress();
                DevCycleRecorder::get().noteGatePass(
                    AssistReason::PassQueueOverflow, "queue-overflow"
                );
                this->noteVerdict(AssistReason::PassQueueOverflow, "queue-overflow");
                this->updateOverlay();
                return finish(true);
            }
        }
        AssistLog::debug(fmt::format(
            "buffer: 2nd DOWN → fire A then plan B (releaseSeen={})",
            this->m_buffer.releaseSeen ? 1 : 0
));
        this->fireBuffered(playLayer, player);
    }
    if (ours && this->m_buffer.state == BufferState::Buffered && !this->m_buffer.down) {
        this->fireBuffered(playLayer, player);
    }

    if (!this->shouldAssistPlayer(player) || !StageConfig::assistPress()) {
        beginDevPress();
        if (this->cpsLimited()) {
            DevCycleRecorder::get().noteGatePass(AssistReason::PassCps, "press-cps");
            this->noteVerdict(AssistReason::PassCps, "press-cps");
        }
        else if (AssistScopeGate::blockReason(playLayer, player)) {
            DevCycleRecorder::get().noteGatePass(AssistReason::PassScope, "press-scope");
            this->noteVerdict(AssistReason::PassScope, "press-scope");
        }
        else {
            DevCycleRecorder::get().noteGatePass(AssistReason::PassAssistOff, "press-off");
            this->noteVerdict(AssistReason::PassAssistOff, "press-off");
        }
        return finish(true);
    }
    if (this->m_attemptTick <= 2) {
        beginDevPress();
        DevCycleRecorder::get().noteGatePass(AssistReason::PassEarlyAttempt, "press-early");
        this->noteVerdict(AssistReason::PassEarlyAttempt, "press-early");
        return finish(true);
    }
    beginDevPress();
    bool const pass = this->planPress(
        playLayer, player, player1, effectTick, firstDt, clockStamp
);
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


