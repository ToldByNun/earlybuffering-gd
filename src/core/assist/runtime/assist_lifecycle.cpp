#include "assist_lifecycle.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../../debug/lag_probe.hpp"
#include "../../../debug/perf_trace.hpp"
#include "../../../dev/capture/dev_cycle_recorder.hpp"
#include "../../../ui/feedback/save_feedback.hpp"
#include "../../../ui/feedback/save_frame_counter.hpp"
#include "../../../ui/overlays/status_overlay.hpp"
#include "../../../ui/visualizers/bake_path_visualizer.hpp"
#include "../../../ui/visualizers/trajectory_visualizer.hpp"
#include "../../bake/bake_map.hpp"
#include "../../bake/bake_verifier.hpp"
#include "../../bake/hybrid_verify_cache.hpp"
#include "../../bake/self_bake_recorder.hpp"
#include "../../diagnostics/assist_real_metrics.hpp"
#include "../../diagnostics/divergence_tracker.hpp"
#include "../../diagnostics/ghost_parity_probe.hpp"
#include "../../learning/level_profile.hpp"
#include "../../sim/shadow_simulator.hpp"
#include "../../sim/sim_trust.hpp"
#include "../../trigger_world/trigger_world_baker.hpp"
#include "../../trigger_world/trigger_world_store.hpp"
#include "../assist_clock.hpp"
#include "../assist_controller.hpp"
#include "../input/click_intent_guard.hpp"

void EbAssistLifecycle::syncButtonState(EbAssistController& controller, PlayLayer* playLayer) {
    if (!playLayer)
        return;
    PlayerObject* players[2] = {playLayer->m_player1, playLayer->m_player2};
    for (int index = 0; index < 2; ++index) {
        auto* player = players[index];
        if (!player)
            continue;
        // Extend-hold after user release: Jump stays down until flushPendingRelease.
        if (controller.m_pendingRelease && controller.m_pendingReleaseP1 == (index == 0) &&
            !controller.m_userHolding[index]) {
            continue;
        }
        auto it = player->m_holdingButtons.find(static_cast<int>(PlayerButton::Jump));
        bool const gdDown = it != player->m_holdingButtons.end() && it->second;
        if (gdDown && !controller.m_userHolding[index]) {
            controller.m_injecting = true;
            player->releaseButton(PlayerButton::Jump);
            controller.m_injecting = false;
            EbAssistLog::debug(
                fmt::format("button sync: released stuck jump button (p{})", index + 1));
        }
    }
}

geode::Result<> EbAssistLifecycle::ensureStartSnapshot(EbAssistController& controller,
                                                     PlayLayer* layer) {
    if (!layer)
        return geode::Err("No layer");
    // Prefer reserved start slot captured at attempt begin. Only (re)capture
    // while still near frame 0 — never overwrite with mid-run pose at bake time.
    if (!controller.m_startSnapshotTaken || controller.m_startSnapshotSlot < 0) {
        if (layer->m_player1 && controller.m_attemptTick <= 5) {
            controller.m_startSnapshotSlot =
                EbShadowSimulator::get().captureStartSnapshot(layer->m_player1);
            controller.m_startSnapshotTaken = controller.m_startSnapshotSlot >= 0;
        }
    }
    if (!controller.m_startSnapshotTaken ||
        controller.m_startSnapshotSlot != EbShadowSimulator::START_SNAPSHOT_SLOT) {
        return geode::Err("Reset the level once (start snapshot), then bake");
    }
    if (!EbShadowSimulator::get().snapshotSlot(controller.m_startSnapshotSlot)) {
        return geode::Err("Start snapshot missing — reset the level, then bake");
    }
    return geode::Ok();
}

geode::Result<int> EbAssistLifecycle::runBakeMap(EbAssistController& controller, PlayLayer* layer,
                                               bool bruteForce) {
    auto snap = controller.ensureStartSnapshot(layer);
    if (snap.isErr())
        return geode::Err(snap.unwrapErr());
    auto method = bruteForce ? BakeMethod::BruteForce : BakeMethod::Replay;
    return EbBakeMap::get().bake(layer, controller.m_startSnapshotSlot, EbStageConfig::bakeRadius(),
                               method);
}

void EbAssistLifecycle::resetLevelProfile(EbAssistController& controller) {
    EbLevelProfile::get().resetCurrent();
    controller.updateOverlay();
}

void EbAssistLifecycle::beginRealUpdate(EbAssistController& controller) {
    // True only while PlayerObject::update runs for real P1
    // (see GameLayerHooks). CBF / mid-step clicks set midStep=true.
    controller.m_inRealUpdate = true;
    controller.m_subDtSeen = 0.f;
}

void EbAssistLifecycle::endRealUpdate(EbAssistController& controller) {
    controller.m_inRealUpdate = false;
}

void EbAssistLifecycle::onSubStep(EbAssistController& controller, PlayerObject* player,
                                float deltaTime) {
    if (!controller.m_inRealUpdate || deltaTime <= 0.f)
        return;
    auto* playLayer = PlayLayer::get();
    if (!playLayer || player != playLayer->m_player1)
        return;
    controller.m_subDtSeen += EbAssistClock::gdStepToSeconds(deltaTime);
}
