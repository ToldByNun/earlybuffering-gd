#include "assist_overlay.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../ui/overlays/dev_overlay.hpp"
#include "../../../ui/overlays/status_overlay.hpp"
#include "../../../ui/visualizers/ai_visualizer.hpp"
#include "../../../ui/visualizers/bake_path_visualizer.hpp"
#include "../../../ui/visualizers/straightfly_visualizer.hpp"
#include "../../../ui/visualizers/trajectory_visualizer.hpp"
#include "../../../ui/visualizers/wave_pattern_visualizer.hpp"
#include "../../bake/bake_map.hpp"
#include "../../diagnostics/assist_perf_stats.hpp"
#include "../../diagnostics/assist_real_metrics.hpp"
#include "../../diagnostics/assist_sim_estimate.hpp"
#include "../../diagnostics/divergence_tracker.hpp"
#include "../../diagnostics/sim_tick_cost_meter.hpp"
#include "../../learning/ai_route_planner.hpp"
#include "../../learning/level_profile.hpp"
#include "../../learning/neural_learning.hpp"
#include "../../sim/planning/search/adaptive/adaptive_plan_search.hpp"
#include "../../sim/sim_trust.hpp"
#include "../../trigger_world/trigger_world_baker.hpp"
#include "../../trigger_world/trigger_world_store.hpp"
#include "../assist_controller.hpp"
#include "../assist_scope_gate.hpp"
#include "../contracts/assist_verdict.hpp"
#include "../flight/ship_line_policy.hpp"
#include "../flight/wave_pattern_recognizer.hpp"
#include <cstring>
#include <string>

void EbAssistOverlay::updateOverlay(EbAssistController& controller) {
    if (!EbStageConfig::showOverlay()) {
        EbStatusOverlay::setVisible(false);
        return;
    }
    auto const stage = EbStageConfig::fromSettings();
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

        auto mode = EbStageConfig::assistMode();
        auto const* modeName = EbStageConfig::assistModeName(mode);
        std::string learnBit;
        if (mode == AssistMode::AI) {
            auto& neural = EbNeuralLearning::get();
            auto const planUs = EbAiRoutePlanner::get().timingOverlayBit();
            if (neural.lastArmedDelay >= 0) {
                learnBit = fmt::format("ai d={} {:.0f}% v{}/{}", neural.lastArmedDelay,
                                       neural.lastArmedConfidence * 100.0f, neural.verifyHits,
                                       neural.verifies);
            } else if (EbStageConfig::autoTune()) {
                learnBit = fmt::format("ai train v{}/{} u{}", neural.verifyHits, neural.verifies,
                                       neural.updates);
            } else {
                learnBit = neural.hasUsableWeights() ? "ai frozen" : "ai no-prior";
            }
            // AP-NL8: seed/expand/verify µs on the AI overlay line.
            if (!planUs.empty())
                learnBit += " " + planUs;
        } else if (EbStageConfig::autoTune()) {
            auto spot =
                EbLevelProfile::get().overlaySpot(controller.m_lastMode, controller.m_lastPlayerX);
            learnBit =
                fmt::format("learn bias{:+d} t{:.1f}",
                            EbAssistControllerImplementation::roundBias(spot.bias), spot.trust);
        } else {
            learnBit = "learn OFF";
        }
        std::string trajBit = EbTrajectoryVisualizer::get().statusHint();
        std::string straightflyBit = EbStraightflyVisualizer::get().statusHint();
        std::string wavePatternBit = EbWavePatternVisualizer::get().statusHint();
        std::string aiVizBit = EbAiVisualizer::get().statusHint();
        int const smartDepth = EbStageConfig::smartDepth();
        std::string smartBit;
        if (smartDepth > 1 && (mode == AssistMode::Live || mode == AssistMode::Hybrid ||
                               mode == AssistMode::AI)) {
            smartBit = fmt::format("smart {}j/{}c", smartDepth, EbStageConfig::smartCombos());
        }
        int const cpsLim = EbStageConfig::cpsLimit();
        std::string cpsBit;
        if (cpsLim > 0) {
            int const cps = controller.currentCps();
            cpsBit = controller.cpsLimited() ? fmt::format("CPS LOCK {}/{}", cps, cpsLim)
                                             : fmt::format("cps {}/{}", cps, cpsLim);
        }
        controller.m_overlayText =
            fmt::format("EarlyBuffer {} | mode {} | {} left={} | {}",
                        EbStageConfig::stageLabel(stage), modeName, state, left, learnBit);
        if (!smartBit.empty())
            controller.m_overlayText += " | " + smartBit;
        if (!cpsBit.empty())
            controller.m_overlayText += " | " + cpsBit;
        if (!trajBit.empty())
            controller.m_overlayText += " | " + trajBit;
        if (!straightflyBit.empty())
            controller.m_overlayText += " | " + straightflyBit;
        if (!wavePatternBit.empty())
            controller.m_overlayText += " | " + wavePatternBit;
        if (!aiVizBit.empty())
            controller.m_overlayText += " | " + aiVizBit;
        if (controller.shipLineAssistActive()) {
            std::string shipLineBit;
            if (controller.m_buffer.state == BufferState::Buffered &&
                controller.m_buffer.shipLineOwned) {
                shipLineBit = fmt::format("sf-corr +{}", controller.m_buffer.planDelay);
            } else if (controller.m_lastShipLineTick >= 0 &&
                       controller.m_attemptTick - controller.m_lastShipLineTick <= 90) {
                shipLineBit = fmt::format("sf-corr +{}", controller.m_lastShipLineDelay);
            } else {
                auto const intent = controller.flightIntent(0);
                if (intent.targetValid && intent.state == EbFlightIntent::State::Tracking) {
                    auto const view = controller.straightflyView(0);
                    double xt = 0.;
                    if (view.valid) {
                        EbShipLinePolicy::Target line;
                        line.enabled = true;
                        line.x = view.x;
                        line.y = view.y;
                        line.degrees = view.degrees;
                        line.bodySize = view.bodySize;
                        line.reversed = view.reversed;
                        line.sideways = view.sideways;
                        if (auto* pl = PlayLayer::get(); pl && pl->m_player1) {
                            xt = EbShipLinePolicy::signedCrossTrack(line, pl->m_player1->m_position.x,
                                                                    pl->m_player1->m_position.y);
                        }
                    }
                    shipLineBit = fmt::format("sf {:.0f}% xt={:+.2f}", intent.confidence * 100., xt);
                } else
                    shipLineBit = "sf arming";
            }
            controller.m_overlayText += " | " + shipLineBit;
        }
        if (controller.wavePatternAssistActive()) {
            std::string waveBit;
            if (controller.m_buffer.state == BufferState::Buffered &&
                controller.m_buffer.wavePatternOwned) {
                waveBit = fmt::format("wp-corr +{}", controller.m_buffer.planDelay);
            } else if (controller.m_lastWavePatternTick >= 0 &&
                       controller.m_attemptTick - controller.m_lastWavePatternTick <= 90) {
                waveBit = fmt::format("wp-corr +{}", controller.m_lastWavePatternDelay);
            } else {
                auto const wave = controller.wavePattern(0);
                if (wave.targetValid && wave.state == EbWavePatternRecognizer::State::Tracking) {
                    waveBit = fmt::format("wp {:.0f}% {:.0f}/{:.0f}ms @{:.0f}°",
                                          wave.confidence * 100., wave.holdSeconds * 1000.,
                                          wave.releaseSeconds * 1000., wave.targetTravelDegrees);
                } else if (wave.state == EbWavePatternRecognizer::State::Acquiring ||
                           wave.state == EbWavePatternRecognizer::State::Reacquiring) {
                    waveBit = "wp arming";
                }
            }
            if (!waveBit.empty())
                controller.m_overlayText += " | " + waveBit;
        } else {
            auto const wave = controller.wavePattern(0);
            if (wave.targetValid && wave.state == EbWavePatternRecognizer::State::Tracking) {
                controller.m_overlayText += fmt::format(
                    " | wp {:.0f}% {:.0f}/{:.0f}ms @{:.0f}°", wave.confidence * 100.,
                    wave.holdSeconds * 1000., wave.releaseSeconds * 1000., wave.targetTravelDegrees);
            } else if (wave.state == EbWavePatternRecognizer::State::Acquiring ||
                       wave.state == EbWavePatternRecognizer::State::Reacquiring) {
                controller.m_overlayText += " | wp arming";
            }
        }
        if (EbStageConfig::triggerBake()) {
            if (EbTriggerWorldBaker::get().running()) {
                controller.m_overlayText += fmt::format(
                    " | tw building {:.0f}%", EbTriggerWorldBaker::get().progress() * 100.f);
            } else {
                auto& tw = EbTriggerWorldStore::get();
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
        if (EbStageConfig::assistOrbs())
            controller.m_overlayText += " | orbs";
        controller.m_overlayText += fmt::format(" | pick={}", EbStageConfig::assistPickName());
        controller.m_overlayText += fmt::format(" | eng={}", EbStageConfig::assistEngineName());
        if (EbStageConfig::assistVerifyAb())
            controller.m_overlayText += " | VERIFY AB ON";
        if (!EbStageConfig::safeMode() && EbStageConfig::isEnabled())
            controller.m_overlayText += " | UNSAFE";
        if (auto* pl = PlayLayer::get()) {
            if (EbAssistScopeGate::isPlatformer(pl->m_player1)) {
                controller.m_overlayText += " | platformer: off";
            }
            if (auto const* reason = EbAssistScopeGate::blockReason(pl, pl->m_player1)) {
                if (std::strcmp(reason, "percent-disable") == 0) {
                    controller.m_overlayText +=
                        fmt::format(" | pct-off @{:.2f}%", pl->getCurrentPercent());
                } else if (std::strcmp(reason, "percent-gate") == 0) {
                    controller.m_overlayText += " | pct-gate";
                }
            }
        }
        {
            auto bit = EbAssistPerfStats::get().overlayBit();
            if (!bit.empty())
                controller.m_overlayText += " | " + bit;
        }
        {
            auto bit = EbSimTickCostMeter::get().overlayBit();
            if (!bit.empty())
                controller.m_overlayText += " | " + bit;
        }
        {
            auto bit = EbAdaptivePlanSearch::overlayBit();
            if (!bit.empty())
                controller.m_overlayText += " | " + bit;
        }
        {
            auto bit = EbDivergenceTracker::get().overlayBit();
            if (!bit.empty())
                controller.m_overlayText += " | " + bit;
            if (controller.m_untrustPassHint)
                controller.m_overlayText += " | UNTRUST pass";
        }
        {
            char const* const trustBit = EbSimTrust::get().overlayBit();
            if (trustBit && trustBit[0] != '\0') {
                controller.m_overlayText += " | ";
                controller.m_overlayText += trustBit;
            }
        }
        {
            // Commit 12: real metrics primary; simEstimate secondary (G10).
            auto const metricsBit = EbAssistRealMetrics::get().overlayBit();
            if (!metricsBit.empty()) {
                controller.m_overlayText += " | " + metricsBit;
            }
            auto const simBit = EbAssistSimEstimate::get().overlayBit();
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
            char const* const verdictBit = EbAssistVerdict::overlayBit(controller.m_lastVerdict);
            if (verdictBit && verdictBit[0] != '\0') {
                controller.m_overlayText += " | ";
                controller.m_overlayText += verdictBit;
            }
        }
    }
    if (EbDevOverlay::shouldShow()) {
        auto const devLine = EbDevOverlay::line();
        if (!devLine.empty()) {
            if (!controller.m_overlayText.empty())
                controller.m_overlayText += " || ";
            controller.m_overlayText += devLine;
        }
    }
    EbStatusOverlay::update(controller.m_overlayText);
}
