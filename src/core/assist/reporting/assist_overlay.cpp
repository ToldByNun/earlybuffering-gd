#include "assist_overlay.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../ui/overlays/dev_overlay.hpp"
#include "../../../ui/overlays/status_overlay.hpp"
#include "../../../ui/visualizers/bake_path_visualizer.hpp"
#include "../../../ui/visualizers/trajectory_visualizer.hpp"
#include "../../bake/bake_map.hpp"
#include "../../diagnostics/assist_perf_stats.hpp"
#include "../../diagnostics/assist_real_metrics.hpp"
#include "../../diagnostics/assist_sim_estimate.hpp"
#include "../../diagnostics/divergence_tracker.hpp"
#include "../../learning/level_profile.hpp"
#include "../../sim/sim_trust.hpp"
#include "../../trigger_world/trigger_world_baker.hpp"
#include "../../trigger_world/trigger_world_store.hpp"
#include "../assist_controller.hpp"
#include "../assist_scope_gate.hpp"
#include "../contracts/assist_verdict.hpp"
#include <cstring>
#include <string>

void AssistOverlay::updateOverlay(AssistController& controller) {
    if (!StageConfig::showOverlay()) {
        StatusOverlay::setVisible(false);
        return;
    }
    auto const stage = StageConfig::fromSettings();
    char const* state = "idle";
    if (controller.m_buffer.state == BufferState::Buffered) {
        if (controller.m_buffer.holdUntilSafe)
            state = "HUS";
        else
            state = controller.m_buffer.down ? "buffer press" : "buffer release";
    } else if (controller.m_buffer.state == BufferState::FiredHolding)
        state = "hold";

    if (!stage.enabled) {
        controller.m_overlayText = "EarlyBuffer: OFF";
    } else {
        int left = 0;
        if (controller.m_buffer.state == BufferState::Buffered)
            left = controller.m_buffer.fireTick - controller.m_attemptTick;

        auto mode = StageConfig::assistMode();
        auto const* modeName = StageConfig::assistModeName(mode);
        std::string learnBit;
        // Learning only applies to Live / Hybrid fallback — not bake windows.
        if (mode == AssistMode::Replay || mode == AssistMode::BruteForce) {
            learnBit = "learn n/a";
        } else if (StageConfig::autoTune()) {
            auto spot =
                LevelProfile::get().overlaySpot(controller.m_lastMode, controller.m_lastPlayerX);
            learnBit =
                fmt::format("learn bias{:+d} t{:.1f}",
                            AssistControllerImplementation::roundBias(spot.bias), spot.trust);
        } else {
            learnBit = "learn OFF";
        }
        // Template ownership: last bake arm/miss hint over map status.
        // Hybrid/Replay/Brute prefix so Live foresight is never confused with tpl.
        std::string bakeBit = controller.m_lastBakeHint.empty() ? BakeMap::get().statusLine()
                                                                : controller.m_lastBakeHint;
        if (!controller.m_lastBakeHint.empty()) {
            char const* pref = nullptr;
            if (mode == AssistMode::Hybrid)
                pref = "hy";
            else if (mode == AssistMode::Replay)
                pref = "rp";
            else if (mode == AssistMode::BruteForce)
                pref = "br";
            if (pref)
                bakeBit = fmt::format("{} {}", pref, controller.m_lastBakeHint);
        }
        // While a bake-armed buffer is waiting, mark tpl ownership on left=.
        if (controller.m_buffer.state == BufferState::Buffered && controller.m_buffer.fromBake) {
            bakeBit = fmt::format("{} · tpl{}", bakeBit, controller.m_buffer.down ? "↓" : "↑");
        }
        std::string trajBit = TrajectoryVisualizer::get().statusHint();
        std::string bakeVizBit = BakePathVisualizer::get().statusHint();
        int const smartDepth = StageConfig::smartDepth();
        std::string smartBit;
        if (smartDepth > 1 && (mode == AssistMode::Live || mode == AssistMode::Hybrid)) {
            smartBit = fmt::format("smart {}j/{}c", smartDepth, StageConfig::smartCombos());
        }
        int const cpsLim = StageConfig::cpsLimit();
        std::string cpsBit;
        if (cpsLim > 0) {
            int const cps = controller.currentCps();
            cpsBit = controller.cpsLimited() ? fmt::format("CPS LOCK {}/{}", cps, cpsLim)
                                             : fmt::format("cps {}/{}", cps, cpsLim);
        }
        controller.m_overlayText =
            fmt::format("EarlyBuffer {} | mode {} | {} left={} | {} | {}",
                        StageConfig::stageLabel(stage), modeName, state, left, learnBit, bakeBit);
        if (!smartBit.empty())
            controller.m_overlayText += " | " + smartBit;
        if (!cpsBit.empty())
            controller.m_overlayText += " | " + cpsBit;
        if (!trajBit.empty())
            controller.m_overlayText += " | " + trajBit;
        if (!bakeVizBit.empty())
            controller.m_overlayText += " | " + bakeVizBit;
        if (StageConfig::triggerBake()) {
            if (TriggerWorldBaker::get().running()) {
                controller.m_overlayText += fmt::format(
                    " | tw building {:.0f}%", TriggerWorldBaker::get().progress() * 100.f);
            } else {
                auto& tw = TriggerWorldStore::get();
                if (!tw.loaded()) {
                    controller.m_overlayText += " | tw off";
                } else if (!tw.hasTracks()) {
                    controller.m_overlayText += " | tw empty";
                } else if (tw.softcapHit()) {
                    controller.m_overlayText +=
                        fmt::format(" | tw {}/{} softcap", tw.readyTrackCount(), tw.trackCount());
                } else {
                    controller.m_overlayText +=
                        fmt::format(" | tw {}/{}", tw.readyTrackCount(), tw.trackCount());
                }
            }
        }
        if (StageConfig::assistOrbs())
            controller.m_overlayText += " | orbs";
        controller.m_overlayText += fmt::format(" | pick={}", StageConfig::assistPickName());
        controller.m_overlayText += fmt::format(" | eng={}", StageConfig::assistEngineName());
        if (StageConfig::assistVerifyAb())
            controller.m_overlayText += " | VERIFY AB ON";
        if (!StageConfig::safeMode() && StageConfig::isEnabled())
            controller.m_overlayText += " | UNSAFE";
        if (auto* pl = PlayLayer::get()) {
            if (AssistScopeGate::isPlatformer(pl->m_player1)) {
                controller.m_overlayText += " | platformer: off";
            }
            if (auto const* reason = AssistScopeGate::blockReason(pl, pl->m_player1)) {
                if (std::strcmp(reason, "percent-disable") == 0) {
                    controller.m_overlayText +=
                        fmt::format(" | pct-off @{:.2f}%", pl->getCurrentPercent());
                } else if (std::strcmp(reason, "percent-gate") == 0) {
                    controller.m_overlayText += " | pct-gate";
                }
            }
        }
        {
            auto bit = AssistPerfStats::get().overlayBit();
            if (!bit.empty())
                controller.m_overlayText += " | " + bit;
        }
        {
            auto bit = DivergenceTracker::get().overlayBit();
            if (!bit.empty())
                controller.m_overlayText += " | " + bit;
            if (controller.m_untrustPassHint)
                controller.m_overlayText += " | UNTRUST pass";
        }
        {
            char const* const trustBit = SimTrust::get().overlayBit();
            if (trustBit && trustBit[0] != '\0') {
                controller.m_overlayText += " | ";
                controller.m_overlayText += trustBit;
            }
        }
        {
            // Commit 12: real metrics primary; simEstimate secondary (G10).
            auto const metricsBit = AssistRealMetrics::get().overlayBit();
            if (!metricsBit.empty()) {
                controller.m_overlayText += " | " + metricsBit;
            }
            auto const simBit = AssistSimEstimate::get().overlayBit();
            if (!simBit.empty()) {
                controller.m_overlayText += " | " + simBit;
            }
        }
        if (controller.m_savesAttempt > 0) {
            controller.m_overlayText +=
                fmt::format(" | saves {}/{}", controller.m_savesAttempt, controller.m_savesLevel);
        }
        if (controller.m_buffer.requireOrb && controller.m_buffer.dMin >= 0) {
            controller.m_overlayText +=
                fmt::format(" | orbWin[{},{}]", controller.m_buffer.dMin, controller.m_buffer.dMax);
        }
        {
            char const* const verdictBit = AssistVerdict::overlayBit(controller.m_lastVerdict);
            if (verdictBit && verdictBit[0] != '\0') {
                controller.m_overlayText += " | ";
                controller.m_overlayText += verdictBit;
            }
        }
    }
    if (DevOverlay::shouldShow()) {
        auto const devLine = DevOverlay::line();
        if (!devLine.empty()) {
            if (!controller.m_overlayText.empty())
                controller.m_overlayText += " || ";
            controller.m_overlayText += devLine;
        }
    }
    StatusOverlay::update(controller.m_overlayText);
}
