#include "desktop_menu_panels.hpp"
#include "../../config/assist_policy_settings.hpp"
#include "../../config/stage_config.hpp"
#include "../../core/assist/assist_controller.hpp"
#include "../../core/assist/flight/flight_intent.hpp"
#include "../../core/assist/flight/wave_pattern_recognizer.hpp"
#include "../../core/learning/hybrid_learning.hpp"
#include "../../core/learning/level_profile.hpp"
#include "../../core/learning/ai_config.hpp"
#include "../../core/learning/ai_route_planner.hpp"
#include "../../core/learning/neural_autoplay.hpp"
#include "../../core/learning/neural_learning.hpp"
#include "../../core/learning/neural_session.hpp"
#include "../../core/platform/ui_capabilities.hpp"
#include "../pause/pause_action_bridge.hpp"
#include "desktop_menu_background.hpp"
#include "desktop_menu_bind.hpp"
#include "desktop_menu_dev_panel.hpp"
#include "desktop_menu_icons.hpp"
#include "desktop_menu_sidebar_material.hpp"
#include "desktop_menu_theme.hpp"
#include "widgets/eb_button.hpp"
#include "widgets/eb_checkbox.hpp"
#include "widgets/eb_color_picker.hpp"
#include "widgets/eb_combo_box.hpp"
#include "widgets/eb_dropdown.hpp"
#include "widgets/eb_element_list.hpp"
#include "widgets/eb_keybind.hpp"
#include "widgets/eb_section_card.hpp"
#include "widgets/eb_slider.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include <cstdio>
#include <imgui.h>

class EbDesktopMenuPanelsDraw {
public:
    static void gap() {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
    }

    static void drawMain() {
        if (EbSectionCard::begin("main-general", "General")) {
            EbCheckbox::drawSetting("enabled", "Enabled");
            EbCheckbox::drawSetting("safe-mode", "Safe Mode");
            {
                char const* opts[] = {"Live", "Hybrid", "AI"};
                EbComboBox::drawSetting("assist-mode", "Assist Mode", opts, 3);
            }
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("main-window", "Window")) {
            {
                char const* opts[] = {"Ticks", "Frames"};
                EbComboBox::drawSetting("window-unit", "Window Unit", opts, 2);
            }
            EbSlider::drawIntSetting("ticks-all", "Window (Default)", 0, 48);
            EbSlider::drawIntSetting("ticks-wave", "Window (Wave)", 0, 48);
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("main-smart", "Smart Prediction")) {
            EbSlider::drawIntSetting("smart-depth", "Smart Depth", 1, 3);
            EbSlider::drawIntSetting("smart-combos", "Smart Combos", 8, 256);
            EbSlider::drawIntSetting("smart-horizon", "Smart Horizon", 0, 320);
            EbSlider::drawIntSetting("robust-k", "Robust Window (±k)", 0, 3);
            EbCheckbox::drawSetting("robust-jitter", "Robust firstDt Jitter");
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("main-adaptive", "Adaptive Prediction")) {
            // Defaults / fallback when a mode's Adaptive Overwrite is off.
            EbCheckbox::drawSetting("adaptive-plan", "Adaptive Plan");
            EbCheckbox::drawSetting("assist-probe-past-horizon", "Probe Past Horizon");
            EbCheckbox::drawSetting("assist-micro-window", "Micro-Window Assist");
            EbSlider::drawIntSetting("adaptive-screen-ticks", "Screen Ticks", 8, 64);
            EbSlider::drawIntSetting("adaptive-verify-quantum", "Verify Quantum", 8, 64);
            EbSlider::drawIntSetting("adaptive-reserve-pct", "Reserve %", 0, 40, "%");
        }
        EbSectionCard::end();
    }

    static void drawAssist() {
        if (EbSectionCard::begin("assist-where", "Where Assist Runs")) {
            EbCheckbox::drawSetting("assist-only-practice", "Only in Practice");
            EbCheckbox::drawSetting("assist-startpos-only", "Only from Start Pos / Test");
            EbSlider::drawIntSetting("assist-percent-min", "Percent Min", 0, 100, "%");
            EbSlider::drawIntSetting("assist-percent-max", "Percent Max", 0, 100, "%");
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("assist-off", "Assist Off Ranges")) {
            EbElementList::drawAssistOffRanges();
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("assist-algorithm", "Algorithm")) {
            EbSlider::drawIntSetting("death-slack", "Death Slack", 0, 16);
            EbSlider::drawIntSetting("look-ahead-cap", "Look-Ahead Cap", 0, 240);
            EbSlider::drawIntSetting("cps-limit", "CPS Limit", 0, 40);
            EbSlider::drawIntSetting("min-click-interval-ms", "Min Click Interval", 0, 500, "ms");
            EbCheckbox::drawSetting("assist-orbs", "Orb Assist");
            {
                char const* policies[] = {"Auto", "On", "Off"};
                EbDropdown::drawSetting("orb-chain-verify", "Orb Chain Verify Default", policies,
                                        3);
            }
            EbSlider::drawIntSetting("sim-budget-us", "Sim Budget", 0, 10000, "µs", true, 100);
            EbSlider::drawIntSetting("tick-budget", "Plan Tick Budget", 500, 20000, "", true, 100);
            EbCheckbox::drawSetting("assist-press", "Assist Presses");
            EbCheckbox::drawSetting("assist-release", "Assist Releases");
        }
        EbSectionCard::end();
        if (EbSectionCard::begin("assist-trigger-world", "Trigger World")) {
            EbCheckbox::drawSetting("trigger-bake", "Prepare Trigger World");
            EbCheckbox::drawSetting("trigger-bake-skip-dynamic", "Skip Dynamic Triggers");
        }
        EbSectionCard::end();

        if (EbUiCapabilities::keybindUI()) {
            if (EbSectionCard::begin("assist-hotkeys", "Hotkeys"))
                EbKeybind::drawSetting("hotkey-assist-mode", "Cycle Assist Mode");
            EbSectionCard::end();
        }
    }

    static void drawLearn() {
        if (EbSectionCard::begin("learn-toggles", "Learning")) {
            EbCheckbox::drawSetting("auto-tune", "Auto-Tune Per Level");
            auto& learning = EbHybridLearning::get();
            if (!EbLevelProfile::get().loaded())
                ImGui::TextWrapped("Open a level to see its learning progress.");
            else if (EbStageConfig::assistMode() != AssistMode::Hybrid)
                ImGui::TextWrapped("Live uses its own timing adjustments. Choose Hybrid to learn "
                                   "which timings work on repeated attempts. AI training lives "
                                   "on the AI tab.");
            else if (!EbStageConfig::autoTuneEnabled())
                ImGui::TextWrapped("Learning is paused. Turn on Auto-Tune to continue with this "
                                   "level's saved experience.");
            else {
                bool frozen = learning.frozen();
                if (EbCheckbox::draw("freeze-hybrid", "Keep current timings", &frozen, true,
                                     "Try your learned timings without updating them. Applies "
                                     "until you leave this level."))
                    learning.freeze(frozen);
                int const familiar = learning.familiar();
                if (frozen)
                    ImGui::TextWrapped("Your timing history is fixed for this test. Hybrid still "
                                       "checks each suggestion as you play.");
                else
                    ImGui::TextWrapped(
                        familiar ? "On familiar clicks, Hybrid checks a timing that worked "
                                   "before. On unfamiliar clicks it uses the usual search."
                                 : "Play the same section a few times. Hybrid learns when a "
                                   "timing keeps working across separate attempts.");
                ImGui::Separator();
                if (ImGui::BeginTable("learning-stats", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("FAMILIAR PATTERNS");
                    ImGui::Text("%d", familiar);
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("TIMINGS ACCEPTED");
                    ImGui::Text("%d / %d", learning.confirmed, learning.tried);
                    ImGui::EndTable();
                }
                float acceptance =
                    learning.tried ? float(learning.confirmed) / learning.tried : 0.f;
                ImGui::ProgressBar(acceptance, ImVec2(-1.f, 5.f), "");
                if (!learning.tried)
                    ImGui::TextWrapped(
                        "No learned timing has reached a physics check this visit. Familiar "
                        "patterns may belong to another part or input type.");
                ImGui::Separator();
                ImGui::Text("Inputs seen this visit   %d", learning.edges);
                ImGui::Text("Local outcomes observed   %d", learning.observations);
                ImGui::Text("Verify probe updates   %d", learning.verifyUpdates);
                ImGui::Text("Action pulls   %u", learning.totalPulls());
                ImGui::Text("Suggested timings chosen   %d / %d", learning.confirmed,
                            learning.tried);
                ImGui::Text("Inputs ending an observation   %d", learning.interrupted);
                ImGui::Text("Local deaths   %d", learning.deaths);
                ImGui::Text("Last delay   %d ticks", learning.lastDelay);
                if (learning.lastDeathAge >= 0)
                    ImGui::Text("Last local death after   %d ticks", learning.lastDeathAge);
                if (learning.drift)
                    ImGui::TextWrapped("Learning paused on %d inputs where the prediction "
                                       "disagreed with the game.",
                                       learning.drift);
            }
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("learn-advanced", "Advanced Learning")) {
            auto& learning = EbHybridLearning::get();
            EbSlider::drawInt("learn-observation", "Observation", &learning.observationTicks, 24,
                              96, " ticks", true,
                              "Longer observation catches later deaths. Another physical input "
                              "ends attribution.");
            EbSlider::drawInt(
                "learn-attempts", "Independent Attempts", &learning.minimumAttempts, 3, 12, "",
                true,
                "More attempts require stronger repeated evidence before proposing a timing.");
            if (EbSlider::drawInt("learn-max-keys", "Max Unique Keys", &learning.maxUniqueKeys, 32,
                                  2048, "", true,
                                  "Hard cap on distinct learned situations for this level. When "
                                  "full, weakest old keys are dropped before new ones are kept.") &&
                EbHybridLearning::enabled())
                learning.applyCapacityLimits();
            if (EbSlider::drawInt("learn-max-actions", "Max Delays Per Spot",
                                  &learning.maxActionsPerSpot, 1, 4, "", true,
                                  "How many different delay timings to remember for one "
                                  "situation. Lower values keep memory tighter.") &&
                EbHybridLearning::enabled())
                learning.applyCapacityLimits();
            EbCheckbox::draw(
                "learn-bridge", "Interpolate Between Survivals", &learning.bridgeInterpolate, true,
                "If 1t and 4t both survived, raise 2t/3t so Hybrid can try the in-between delays.");
            EbCheckbox::draw(
                "learn-hybrid-first", "Verify Learned First", &learning.preferHybridFirst, true,
                "Check known Hybrid delays (best first) with Live physics before the ordinary "
                "search. The first verified-safe learned delay wins.");
            if (EbSlider::drawFloat("learn-ucb", "UCB Exploration", &learning.ucbCoefficient, 0.f,
                                    4.f, 2, "", true,
                                    "How strongly under-tried delays rise in the verify order. "
                                    "Zero ranks by score only; higher values explore more.") &&
                EbHybridLearning::enabled())
                learning.applyCapacityLimits();
            if (EbSlider::drawFloat("learn-neighbor", "Neighbor Bleed", &learning.neighborBleed, 0.f,
                                    1.f, 2, "", true,
                                    "Share a fraction of each reward update with delay±1 so nearby "
                                    "timings generalize without waiting for separate attempts.") &&
                EbHybridLearning::enabled())
                learning.applyCapacityLimits();
            EbSlider::drawInt(
                "learn-eligibility", "Eligibility Trace", &learning.eligibilitySteps, 0, 4, "", true,
                "On a local death, also soft-penalize this many recently successful delivered "
                "timings in the same attempt (temporal credit assignment).");
            ImGui::TextDisabled("Keys stored   %d / %d", learning.uniqueKeys(),
                                learning.maxUniqueKeys);
            EbCheckbox::draw("learn-outcomes", "Track outcome counts", &learning.trackOutcomes,
                             true,
                             "Keep separate success, failure and uncertain-outcome counts for "
                             "learned timings. Turning this off stops collecting these extra "
                             "details; saved evidence is retained.");
            EbCheckbox::draw("learn-phase", "Record death timing phase", &learning.trackPhase,
                             learning.trackOutcomes,
                             "Record where within a physics tick an input happened, to help "
                             "inspect timing-sensitive deaths. Requires Track outcome counts.");
            EbCheckbox::draw("learn-sequence", "Record recent input sequence",
                             &learning.trackSequence, learning.trackOutcomes,
                             "Record a compact signature of recent presses and releases to help "
                             "compare input patterns. Requires Track outcome counts.");
            EbSlider::drawInt("learn-death-penalty", "Local death penalty", &learning.deathPenalty,
                              0, 4, "", true,
                              "How strongly an attributed local death lowers a timing score. Zero "
                              "disables the score penalty. Higher values react faster but can "
                              "discard useful timings when deaths have other causes.");
            EbSlider::drawInt("learn-survival-reward", "Survival reward", &learning.survivalReward,
                              1, 3, "", true,
                              "How strongly an ordinary observed survival raises a timing score. "
                              "Higher values favor successful timings sooner; survival alone does "
                              "not prove that a delay rescued the input.");
            EbSlider::drawInt("learn-lineup-depth", "Lineup Look-Ahead", &learning.lineupDepth, 1,
                              3, " inputs", true,
                              "On familiar presses, check up to this many inputs ahead within the "
                              "current planning budget.");
            EbSlider::drawInt(
                "learn-bin-x", "Position X resolution", &learning.positionBinX, 16, 256, "", true,
                "Width of each horizontal position group in level units. Smaller values "
                "distinguish nearby timings; larger values share evidence across a wider area.");
            EbSlider::drawInt(
                "learn-bin-y", "Position Y resolution", &learning.positionBinY, 16, 256, "", true,
                "Height of each vertical position group in level units. Smaller values separate "
                "paths more precisely; larger values make moving paths easier to recognize again.");
            EbSlider::drawInt("learn-tps-quantum", "TPS grouping", &learning.tpsQuantum, 1, 120, "",
                              true,
                              "Group physics rates in steps of this many ticks per second. Larger "
                              "groups tolerate small rate differences; smaller groups keep "
                              "different physics rates separate.");
            EbCheckbox::draw(
                "learn-match-substep", "Distinguish sub-step inputs", &learning.matchSubstep, true,
                "Keep inputs from shortened physics steps separate from full-step inputs. More "
                "precise matching can reduce how often a learned pattern is recognized.");
            EbCheckbox::draw("learn-match-intervals", "Distinguish input intervals",
                             &learning.matchIntervals, true,
                             "Use coarse recent input-interval groups when recognizing a pattern, "
                             "helping distinguish short taps and longer holds. Turning this off "
                             "shares evidence more broadly.");
            EbCheckbox::draw(
                "learn-coarse", "Continuous mode fallback", &learning.coarseFallback, true,
                "In Ship, Wave and Swing, also learn broader position groups so nearby paths can "
                "share experience. Suggested timings still need current physics verification.");
            EbSlider::drawInt(
                "learn-coarse-attempts", "Fallback independent attempts", &learning.coarseAttempts,
                3, 12, "", learning.coarseFallback,
                "Minimum independent attempts required before using the broader continuous-mode "
                "evidence. Higher values are more cautious. Requires Continuous mode fallback.");
            ImGui::TextWrapped(
                "Coarser groups share experience between nearby paths. Every suggestion is checked "
                "against current geometry. These controls apply during this level visit.");
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("learn-actions", "Config")) {
            // No gap() between rows — ItemSpacing alone keeps H/V grid even.
            EbButton::beginRow(2);
            EbButton::drawAction("export-config-action", "Export Config");
            EbButton::drawAction("import-config-action", "Import Config");
            EbButton::endRow();
            EbButton::beginRow(2);
            EbButton::drawAction("reset-learn-action", "Reset This Level");
            EbButton::drawAction("browse-cloud-configs-action", "Cloud Configs");
            EbButton::endRow();
            ImGui::TextDisabled("Reset clears Hybrid tables and AI weights for this level.");
        }
        EbSectionCard::end();

        if (EbUiCapabilities::keybindUI()) {
            if (EbSectionCard::begin("learn-hotkeys", "Hotkeys")) {
                EbKeybind::drawSetting("hotkey-auto-tune", "Auto-Tune");
                EbKeybind::drawSetting("hotkey-reset-level", "Reset Level Learning");
            }
            EbSectionCard::end();
        }
    }

    static void drawAi() {
        auto& cfg = EbAiConfig::get();
        auto& neural = EbNeuralLearning::get();
        auto& autoplay = EbNeuralAutoplay::get();
        auto& planner = EbAiRoutePlanner::get();
        bool const aiOn = EbStageConfig::assistMode() == AssistMode::AI;

        auto dirty = [&]() {
            cfg.markCustom();
            cfg.save();
        };
        auto applyPreset = [&](EbAiConfig::Preset p) {
            cfg.applyPreset(p);
            cfg.save();
            neural.preferAiFirst = cfg.preferAiFirst;
            neural.topK = std::clamp(cfg.maxCandidates, 1, 8);
            neural.learningRate = cfg.learningRate;
            autoplay.syncFromConfig();
            autoplay.syncTimeWarp(PlayLayer::get());
        };

        // —— Status (Learn-style compact header) ——
        if (EbSectionCard::begin("ai-status", "Candidate Scorer")) {
            if (!aiOn)
                ImGui::TextWrapped("Choose Assist Mode → AI on Main to use the neural scorer.");
            else if (!EbLevelProfile::get().loaded())
                ImGui::TextWrapped("Open a level to load or train per-level .neural weights.");
            else {
                bool frozen = neural.frozen();
                if (EbCheckbox::draw("freeze-ai", "Keep current weights", &frozen, true,
                                     "Infer without updating weights until you leave this level.")) {
                    neural.freeze(frozen);
                    cfg.freezeWeights = frozen;
                    dirty();
                }
                if (!neural.hasUsableWeights())
                    ImGui::TextWrapped(
                        "No usable weights yet. Play with Auto-Tune on, or load a prior below.");
                else if (frozen)
                    ImGui::TextWrapped(
                        "Weights are fixed for this visit. AI still ranks and verifies before arming.");
                else if (!EbStageConfig::autoTuneEnabled())
                    ImGui::TextWrapped(
                        "Auto-Tune is off: AI infers but does not train. Enable it on Learn.");
                else
                    ImGui::TextWrapped(
                        "Training on verified outcomes. Routes are scored, gated, then Shadow-verified.");
                ImGui::Separator();
                if (ImGui::BeginTable("ai-stats", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("VERIFY HITS");
                    ImGui::Text("%d / %d", neural.verifyHits, neural.verifies);
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("LAST ARMED");
                    if (neural.lastArmedDelay >= 0)
                        ImGui::Text("d=%d  %.0f%%", neural.lastArmedDelay,
                                    neural.lastArmedConfidence * 100.0f);
                    else
                        ImGui::TextDisabled("—");
                    ImGui::EndTable();
                }
                float hitRate =
                    neural.verifies ? float(neural.verifyHits) / float(neural.verifies) : 0.f;
                ImGui::ProgressBar(hitRate, ImVec2(-1.f, 5.f), "");
                ImGui::Separator();
                ImGui::Text("Inferences / updates   %d / %d", neural.inferences, neural.updates);
                ImGui::Text("Route plans / invalid   %d / %d", planner.plans, planner.invalidations);
                ImGui::Text("Route verify   %d / %d", planner.verifyHits, planner.verifyTries);
                {
                    auto const timing = planner.timingOverlayBit();
                    if (!timing.empty())
                        ImGui::TextDisabled("%s", timing.c_str());
                }
                ImGui::Text("Session samples / attempts   %d / %d", EbNeuralSession::get().size(),
                            EbNeuralSession::get().attemptCount());
            }
        }
        EbSectionCard::end();

        // —— Preset + policy ——
        if (EbSectionCard::begin("ai-general", "General")) {
            {
                char const* presets[] = {"Nova Reference", "Balanced", "Performance", "Accuracy",
                                         "Experimental", "Custom"};
                int presetIdx = static_cast<int>(cfg.preset);
                if (presetIdx < 0 || presetIdx > 5)
                    presetIdx = 5;
                if (EbDropdown::draw("ai-preset", "Preset", presets, 6, &presetIdx, nullptr, true,
                                     "Applies a full profile. Custom appears after you tweak knobs.")) {
                    if (presetIdx <= 4)
                        applyPreset(static_cast<EbAiConfig::Preset>(presetIdx));
                }
            }
            {
                char const* backends[] = {"Candidate Sigmoid", "Dual-Head (Experimental)"};
                int backend = static_cast<int>(cfg.backend);
                if (EbComboBox::draw("ai-backend", "Backend", backends, 2, &backend,
                                     cfg.experimentalBackends || backend == 0,
                                     "Nova-style single sigmoid, or experimental 49-way + confidence.")) {
                    cfg.backend = static_cast<EbAiConfig::Backend>(backend);
                    dirty();
                }
            }
            {
                char const* policies[] = {"Neural → Sim", "Sim First", "Hybrid Teacher",
                                          "Neural Only (unsafe)"};
                int policy = static_cast<int>(cfg.policy);
                if (EbComboBox::draw("ai-policy", "Policy", policies, 4, &policy,
                                     cfg.experimentalBackends || policy != 3,
                                     "How candidates are proposed vs Shadow-verified.")) {
                    cfg.policy = static_cast<EbAiConfig::Policy>(policy);
                    dirty();
                }
            }
            if (EbCheckbox::draw("ai-cfg-prefer", "Verify AI First", &cfg.preferAiFirst, aiOn,
                                 "Check neural candidates before ordinary Live search.")) {
                neural.preferAiFirst = cfg.preferAiFirst;
                dirty();
            }
        }
        EbSectionCard::end();

        // —— Scorer / candidates / gate ——
        if (EbSectionCard::begin("ai-scorer", "Scorer")) {
            {
                char const* encs[] = {"Linear / maxDelay", "log1p (Nova)"};
                int enc = static_cast<int>(cfg.delayEncoding);
                if (EbComboBox::draw("ai-delay-enc", "Delay Encoding", encs, 2, &enc, true,
                                     "Linear keeps existing priors. log1p matches Nova packing.")) {
                    cfg.delayEncoding = static_cast<EbAiConfig::DelayEncoding>(enc);
                    dirty();
                }
            }
            if (EbSlider::drawInt("ai-cand-max", "Top-K / Max Candidates", &cfg.maxCandidates, 1, 32,
                                  "", true, "How many delays to score and verify.")) {
                neural.topK = std::clamp(cfg.maxCandidates, 1, 8);
                dirty();
            }
            if (EbCheckbox::draw("ai-adapt-samp", "Adaptive Delay Table", &cfg.adaptiveSampling, true,
                                 "Nova-style scaled delay table instead of a fixed step."))
                dirty();
            if (!cfg.adaptiveSampling &&
                EbSlider::drawInt("ai-cand-step", "Sample Step", &cfg.sampleStep, 1, 8, "", true,
                                  "Fixed stride when adaptive sampling is off."))
                dirty();
            {
                char const* seeds[] = {"Enhanced (product)", "NovaParity (compare)"};
                int seedIdx = static_cast<int>(cfg.seedStrategy);
                if (seedIdx < 0 || seedIdx > 1)
                    seedIdx = 0;
                if (EbComboBox::draw(
                        "ai-seed-strategy", "Seed Strategy", seeds, 2, &seedIdx, true,
                        "Enhanced: config threshold + top-k + Shadow. NovaParity: duty + "
                        "subsample + hard 0.6 (research A/B only).")) {
                    cfg.seedStrategy = static_cast<EbAiConfig::SeedStrategy>(seedIdx);
                    dirty();
                }
            }
            if (cfg.seedStrategy == EbAiConfig::SeedStrategy::NovaParity) {
                if (EbSlider::drawInt("ai-seed-duty-period", "Seed Duty Period", &cfg.seedDutyPeriod,
                                      1, 16, "", true,
                                      "NovaParity only: seed when planTick % period == phase."))
                    dirty();
                if (EbSlider::drawInt("ai-seed-duty-phase", "Seed Duty Phase", &cfg.seedDutyPhase, 0,
                                      std::max(0, cfg.seedDutyPeriod - 1), "", true,
                                      "Phase offset for the NovaParity duty bit."))
                    dirty();
            }
            ImGui::Separator();
            if (EbCheckbox::draw("ai-gate-on", "Confidence Gating", &cfg.gatingEnabled, true,
                                 "Drop low-confidence candidates before Shadow verify."))
                dirty();
            if (EbSlider::drawFloat(
                    "ai-gate-thr", "Confidence Threshold", &cfg.confidenceThreshold, 0.05f, 0.95f, 2,
                    "",
                    cfg.gatingEnabled && cfg.seedStrategy == EbAiConfig::SeedStrategy::Enhanced,
                    "Enhanced floor only — NovaParity uses a hard 0.60 gate."))
                dirty();
            if (EbCheckbox::draw("ai-gate-adapt", "Adaptive Threshold", &cfg.adaptiveThreshold,
                                 cfg.gatingEnabled,
                                 "Raise the floor to a percentile of scored candidates."))
                dirty();
            if (EbSlider::drawFloat("ai-gate-orb", "Orb Hint Confidence", &cfg.orbHintConfidence,
                                    0.05f, 0.9f, 2, "", true,
                                    "Soft escape near orbs/pads when the hard gate finds nothing."))
                dirty();
            if (EbSlider::drawFloat("ai-lr", "Learning Rate", &cfg.learningRate, 0.001f, 0.05f, 3, "",
                                    aiOn, "On-play weight step when Auto-Tune is on.")) {
                neural.learningRate = cfg.learningRate;
                dirty();
            }
        }
        EbSectionCard::end();

        // —— Route + Shadow ——
        if (EbSectionCard::begin("ai-route", "Route + Shadow")) {
            {
                char const* algos[] = {"Greedy Beam", "Depth First"};
                int algo = static_cast<int>(cfg.searchAlgo);
                if (EbComboBox::draw("ai-search-algo", "Search", algos, 2, &algo, true,
                                     "How multi-event routes are expanded.")) {
                    cfg.searchAlgo = static_cast<EbAiConfig::SearchAlgo>(algo);
                    dirty();
                }
            }
            if (EbSlider::drawInt("ai-route-depth", "Max Route Depth", &cfg.maxRouteDepth, 1, 8, "",
                                  true, "Press/release events per plan."))
                dirty();
            if (EbSlider::drawInt("ai-route-beam", "Beam Width", &cfg.beamWidth, 1, 8, "", true,
                                  "Verified first-events kept in the beam."))
                dirty();
            if (EbSlider::drawInt("ai-route-commit", "Committed Events", &cfg.maxCommittedEvents, 1,
                                  8, "", true, "Future inputs Autoplay may arm."))
                dirty();
            if (EbSlider::drawInt("ai-route-horizon", "Planning Horizon", &cfg.planningHorizon, 16,
                                  480, " t", true, "Future tick window."))
                dirty();
            if (EbCheckbox::draw("ai-route-release", "Generate Releases", &cfg.generateRelease, true,
                                 "Plan hold/release as event 2 when the mode supports holds."))
                dirty();
            ImGui::Separator();
            if (EbCheckbox::draw("ai-sim-on", "Shadow Verify", &cfg.simulationEnabled, true,
                                 "Off only for unsafe neural-only experiments."))
                dirty();
            if (EbSlider::drawInt("ai-sim-us", "Verify Budget", &cfg.simBudgetUs, 200, 8000, " µs",
                                  cfg.simulationEnabled, "Per-candidate verifyDelay budget."))
                dirty();
            if (EbSlider::drawInt("ai-sim-calls", "Max Sim Calls", &cfg.maxSimCalls, 1, 64, "",
                                  cfg.simulationEnabled, "Hard cap per plan()."))
                dirty();
            if (EbSlider::drawInt("ai-route-budget", "Search Timeout", &cfg.searchTimeoutUs, 400,
                                  12000, " µs", true, "Wall budget for one plan() call."))
                dirty();
            if (EbCheckbox::draw("ai-sim-unknown", "Reject Unknown", &cfg.rejectUnknown,
                                 cfg.simulationEnabled,
                                 "Drop candidates when Shadow returns unknown."))
                dirty();
        }
        EbSectionCard::end();

        // —— Autoplay ——
        if (EbSectionCard::begin("ai-autoplay", "Autoplay + Session")) {
            bool autoplayOn = autoplay.enabled();
            if (EbCheckbox::draw("ai-autoplay", "Autoplay", &autoplayOn, aiOn,
                                 "Multi-event route planner + Shadow-verified Jump schedule.")) {
                cfg.autoplay = autoplayOn;
                autoplay.setEnabled(autoplayOn);
                autoplay.syncTimeWarp(PlayLayer::get());
                dirty();
            }
            if (autoplayOn && aiOn && !EbStageConfig::autoTuneEnabled())
                ImGui::TextColored(ImVec4(1.f, 0.75f, 0.35f, 1.f),
                                   "Auto-Tune is off — Autoplay will probe but not train.");
            if (EbCheckbox::draw("ai-autoplay-restart", "Restart on Death", &cfg.restartOnDeath,
                                 aiOn && autoplayOn,
                                 "Reset the attempt shortly after death.")) {
                autoplay.setRestartOnDeath(cfg.restartOnDeath);
                dirty();
            }
            if (EbSlider::drawFloat("ai-autoplay-warp", "Time Warp", &cfg.timeWarp, 1.f, 32.f, 0, "x",
                                    aiOn && autoplayOn,
                                    "Speeds up attempts while Autoplay is on.")) {
                autoplay.setTimeWarp(cfg.timeWarp);
                autoplay.syncTimeWarp(PlayLayer::get());
                dirty();
            }
            float explorePct = cfg.exploreRate * 100.f;
            if (EbSlider::drawFloat("ai-autoplay-explore", "Explore", &explorePct, 0.f, 25.f, 0, "%",
                                    aiOn && autoplayOn,
                                    "Chance to press when no route is found.")) {
                cfg.exploreRate = explorePct / 100.f;
                autoplay.setExploreRate(cfg.exploreRate);
                dirty();
            }
            if (EbSlider::drawInt("ai-autoplay-queue", "Route Queue", &cfg.maxRouteQueue, 0, 8, "",
                                  aiOn && autoplayOn,
                                  "Early-plan depth (Autoplay only — does not touch Live).")) {
                autoplay.syncFromConfig();
                dirty();
            }
            if (cfg.maxRouteQueue > 0 &&
                EbSlider::drawInt("ai-autoplay-early", "Early Plan Ticks", &cfg.earlyPlanTicks, 0, 64,
                                  "", aiOn && autoplayOn,
                                  "Enqueue next SV route when remaining delay ≤ this.")) {
                autoplay.syncFromConfig();
                dirty();
            }
            if (EbSlider::drawInt("ai-autoplay-cps", "Max Autoplay CPS", &cfg.maxAutoplayCps, 1, 60,
                                  "", aiOn && autoplayOn,
                                  "Soft press cap per 240 physics ticks (Autoplay only).")) {
                autoplay.syncFromConfig();
                dirty();
            }
            if (EbCheckbox::draw("ai-autoplay-memory", "Memory Reuse Route", &cfg.memoryReuseRoute,
                                 aiOn && autoplayOn,
                                 "Reuse last ShadowVerified route briefly if replan fails.")) {
                autoplay.syncFromConfig();
                dirty();
            }
            if (cfg.memoryReuseRoute &&
                EbSlider::drawInt("ai-autoplay-mem-hold", "Memory Hold", &cfg.memoryHoldTicks, 4, 240,
                                  " t", aiOn && autoplayOn,
                                  "How long a remembered SV route stays valid.")) {
                autoplay.syncFromConfig();
                dirty();
            }
            ImGui::Text("Probes / presses / explore   %d / %d / %d", autoplay.probes(),
                        autoplay.presses(), autoplay.explorePresses());
            ImGui::Text("Route events / queue / last   %d / %d / %s", autoplay.routeEvents(),
                        autoplay.queuedRoutes(), autoplay.lastProbeReason());
            {
                auto const& m = autoplay.metrics();
                ImGui::TextDisabled("DR4 fidelity %.0f%%  verify %.0f%%  stuck %d  cpsThrottle %d",
                                    m.routeFidelityRate() * 100.f, m.verifyCompletenessRate() * 100.f,
                                    m.stuckRecoveries(), m.cpsThrottles());
            }
            ImGui::Separator();
            if (EbCheckbox::draw("ai-persist-sessions", "Persist Sessions to Disk",
                                 &cfg.persistSessions, aiOn,
                                 "On attempt end, write new IL/RL samples to "
                                 "saveDir/ai/sessions/*.ebns for later training."))
                dirty();
            if (EbButton::draw("ai-train-session", "Train Session",
                               aiOn && EbLevelProfile::get().loaded() &&
                                   EbNeuralSession::get().size() > 0,
                               "Batch-train from the session buffer, then save.")) {
                int const n = neural.trainSession(cfg.trainStepsPerUpdate, true);
                ImGui::Text("Trained %d updates", n);
            }
            if (EbButton::draw("ai-load-disk-sessions", "Load Disk Sessions", aiOn,
                               "Append saveDir/ai/sessions/*.ebns into the buffer.")) {
                int const added =
                    EbNeuralSession::get().loadDirectory(EbNeuralSession::sessionsDir());
                ImGui::Text("Loaded %d samples", added);
            }
            if (EbButton::draw("ai-train-disk", "Train From Disk Sessions",
                               aiOn && EbLevelProfile::get().loaded(),
                               "Load all on-disk sessions into the buffer, then train.")) {
                int const added =
                    EbNeuralSession::get().loadDirectory(EbNeuralSession::sessionsDir());
                int const n = neural.trainSession(cfg.trainStepsPerUpdate, true);
                ImGui::Text("Loaded %d · trained %d", added, n);
            }
            if (EbButton::draw("ai-clear-session", "Clear Session",
                               EbNeuralSession::get().size() > 0 ||
                                   EbNeuralSession::get().attemptCount() > 0,
                               "Drop session samples without touching weights."))
                EbNeuralSession::get().clear();
            ImGui::TextDisabled("Buffer %d · attempts %d · files saved %d",
                                EbNeuralSession::get().size(),
                                EbNeuralSession::get().attemptCount(),
                                EbNeuralSession::get().filesSaved());
            if (EbNeuralSession::get().lastTrainUpdates() > 0)
                ImGui::TextDisabled("Last train: %d updates",
                                    EbNeuralSession::get().lastTrainUpdates());
            if (!EbNeuralSession::get().lastPersistPath().empty())
                ImGui::TextDisabled("%s", EbNeuralSession::get().lastPersistPath().c_str());
        }
        EbSectionCard::end();

        // —— Prior + viz + advanced ——
        if (EbSectionCard::begin("ai-prior", "Prior + Visual")) {
            bool trainBase = neural.trainBasePrior();
            if (EbCheckbox::draw("ai-train-base", "Train Base Prior", &trainBase, aiOn,
                                 "Write IL/RL into shared saveDir/ai/prior.ebnn across levels."))
                neural.setTrainBasePrior(trainBase);
            if (EbButton::draw("ai-save-base", "Save as Base Prior", aiOn && neural.loaded(),
                               "Copy current weights to the user base prior."))
                (void)neural.saveAsBasePrior();
            if (EbButton::draw("ai-load-prior", "Load Base / Packaged Prior",
                               aiOn && EbLevelProfile::get().loaded(),
                               "Load saveDir/ai prior.ebnn or model.bin (NVM2), else packaged prior."))
                (void)neural.loadPrior();
            if (EbButton::draw("ai-reset-weights", "Reset AI Weights",
                               aiOn && EbLevelProfile::get().loaded(),
                               "Clear active weights. Hybrid tables stay."))
                neural.reset();
            ImGui::Separator();
            EbCheckbox::drawSetting("show-ai", "Visualize AI");
            if (EbCheckbox::draw("ai-viz-route", "Show Planned Route", &cfg.showRoute, true,
                                 "Press/release markers when Visualize AI is on."))
                dirty();
            if (EbCheckbox::draw("ai-viz-rej", "Show Rejected Candidates", &cfg.showRejected, true,
                                 "Draw gated-out candidates."))
                dirty();
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("ai-advanced", "Advanced")) {
            if (EbCheckbox::draw("ai-exp", "Experimental Backends", &cfg.experimentalBackends, true,
                                 "Unlock Dual-Head backend and Neural-Only policy."))
                dirty();
            if (EbCheckbox::draw("ai-debug", "Debug Logging", &cfg.debugLogging, true,
                                 "Extra Assist log lines for plan/gate decisions."))
                dirty();
            if (EbCheckbox::draw("ai-replan-div", "Replan on Divergence", &cfg.replanOnDivergence,
                                 true, "Invalidate the route when live X drifts too far."))
                dirty();
            if (EbButton::draw("ai-reset-cfg", "Reset AI Config", true,
                               "Restore Balanced defaults (weights untouched).")) {
                cfg.resetAll();
                cfg.save();
                autoplay.syncFromConfig();
            }
            ImGui::TextDisabled("Persists to saveDir/ai/config-v1.json");
        }
        EbSectionCard::end();
    }

    static void drawGamemodeBody(char const* title, char const* prefix) {
        if (!title || !prefix) {
            return;
        }
        char enableKey[64];
        char overwriteKey[64];
        char ticksKey[64];
        char capKey[64];
        char slackKey[64];
        char latencyKey[64];
        char adaptiveOwKey[64];
        char adaptivePlanKey[64];
        char adaptiveScreenKey[64];
        char adaptiveVerifyKey[64];
        char adaptiveReserveKey[64];
        std::snprintf(enableKey, sizeof(enableKey), "%s-enable", prefix);
        std::snprintf(overwriteKey, sizeof(overwriteKey), "%s-overwrite", prefix);
        std::snprintf(ticksKey, sizeof(ticksKey), "%s-ticks", prefix);
        std::snprintf(capKey, sizeof(capKey), "%s-cap", prefix);
        std::snprintf(slackKey, sizeof(slackKey), "%s-slack", prefix);
        std::snprintf(latencyKey, sizeof(latencyKey), "%s-latency", prefix);
        std::snprintf(adaptiveOwKey, sizeof(adaptiveOwKey), "%s-adaptive-overwrite", prefix);
        std::snprintf(adaptivePlanKey, sizeof(adaptivePlanKey), "%s-adaptive-plan", prefix);
        std::snprintf(adaptiveScreenKey, sizeof(adaptiveScreenKey), "%s-adaptive-screen-ticks",
                      prefix);
        std::snprintf(adaptiveVerifyKey, sizeof(adaptiveVerifyKey), "%s-adaptive-verify-quantum",
                      prefix);
        std::snprintf(adaptiveReserveKey, sizeof(adaptiveReserveKey), "%s-adaptive-reserve-pct",
                      prefix);

        EbCheckbox::drawSetting(enableKey, "Enable");
        EbCheckbox::drawSetting(overwriteKey, "Overwrite");
        EbSlider::drawIntSetting(ticksKey, "Window", 0, 32);
        EbSlider::drawIntSetting(capKey, "Look-Ahead", 0, 240);
        EbSlider::drawIntSetting(slackKey, "Slack", -1, 16);
        EbSlider::drawFloatSetting(latencyKey, "Latency", 0.0f, 2.0f, 2);

        char chainKey[64];
        std::snprintf(chainKey, sizeof(chainKey), "%s-orb-chain-verify", prefix);
        char const* choices[] = {"Inherit", "Auto", "On", "Off"};
        EbDropdown::drawSetting(chainKey, "Orb Chain Verify", choices, 4);
        if (std::string(prefix) == "ball")
            EbDropdown::drawSetting("ball-ground-check", "Ground Check", choices + 1, 3);
        if (!EbLevelProfile::get().key().empty()) {
            auto drawLevelPolicy = [&](char const* key, char const* label) {
                auto value = EbAssistPolicySettings::levelValue(key);
                int selected = 0;
                for (int index = 1; index < 4; ++index) {
                    if (value == choices[index])
                        selected = index;
                }
                if (EbDropdown::draw((std::string("level-") + key).c_str(), label, choices, 4,
                                     &selected))
                    EbAssistPolicySettings::setLevelValue(key, choices[selected]);
            };
            drawLevelPolicy(chainKey, "This Level: Orb Chain Verify");
            if (std::string(prefix) == "ball")
                drawLevelPolicy("ball-ground-check", "This Level: Ground Check");
        }

        gap();
        EbCheckbox::drawSetting(adaptiveOwKey, "Adaptive Overwrite");
        bool const adaptiveOw = EbDesktopMenuBind::getBool(adaptiveOwKey, false);
        EbCheckbox::drawSetting(adaptivePlanKey, "Adaptive Plan", adaptiveOw);
        EbSlider::drawIntSetting(adaptiveScreenKey, "Screen Ticks", 8, 64, "", adaptiveOw);
        EbSlider::drawIntSetting(adaptiveVerifyKey, "Verify Quantum", 8, 64, "", adaptiveOw);
        EbSlider::drawIntSetting(adaptiveReserveKey, "Reserve %", 0, 40, "%", adaptiveOw);
        (void)title;
    }

    static void drawModes() {
        static int selected = 0;
        if (selected < 0 || selected >= EbDesktopMenuIcons::kGamemodeCount) {
            selected = 0;
        }

        if (EbSectionCard::begin("modes-picker", "Modes")) {
            EbDesktopMenuIcons::drawGamemodeBar(&selected);
        }
        EbSectionCard::end();

        char const* title = EbDesktopMenuIcons::gamemodeTitle(selected);
        char const* prefix = EbDesktopMenuIcons::gamemodePrefix(selected);
        if (EbSectionCard::begin("modes-settings", "Settings")) {
            drawGamemodeBody(title, prefix);
        }
        EbSectionCard::end();
    }

    static void drawFlight() {
        if (EbSectionCard::begin("flight-assist", "Assist")) {
            EbCheckbox::drawSetting("ship-line-enable", "Straightfly Assist");
            EbCheckbox::drawSetting("wave-pattern-enable", "Wave Pattern Assist");
            EbSlider::drawIntSetting("wave-angle-snap", "Flight Angle Snap", 0, 45, " deg");
            EbSlider::drawFloatSetting("ship-room-of-error", "Flight Room of Error", .4f, 3.f, 1,
                                       "x");
            ImGui::TextDisabled("Assist arms corrections. Visual tab only draws the lines.");
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("flight-status", "Status")) {
            auto& controller = EbAssistController::get();
            if (!EbLevelProfile::get().loaded())
                ImGui::TextWrapped("Open a level to see Ship / Wave tracking status.");
            else {
                for (int player = 0; player < 2; ++player) {
                    auto const intent = controller.flightIntent(player);
                    ImGui::Text("P%d Ship: %s   %.0f%%", player + 1,
                                EbFlightIntent::stateName(intent.state), intent.confidence * 100.);
                    if (intent.targetValid)
                        ImGui::Text("  Target %.1f°   Hold %.0f / Release %.0f ms",
                                    intent.targetDegrees, intent.holdSeconds * 1000.,
                                    intent.releaseSeconds * 1000.);
                    auto const wave = controller.wavePattern(player);
                    ImGui::Text("P%d Wave: %s   %.0f%%", player + 1,
                                EbWavePatternRecognizer::stateName(wave.state),
                                wave.confidence * 100.);
                    if (wave.targetValid)
                        ImGui::Text("  Hold %.0f / Release %.0f ms   duty %.0f%%",
                                    wave.holdSeconds * 1000., wave.releaseSeconds * 1000.,
                                    wave.dutyCycle * 100.);
                }
            }
            ImGui::TextDisabled("Manual motif / angle overrides stay on Dev.");
        }
        EbSectionCard::end();
    }

    static void drawVisual() {
        if (EbSectionCard::begin("visual-viz", "Visualizers")) {
            EbCheckbox::drawSetting("show-trajectory", "Trajectory Visualizer");
            EbCheckbox::drawSetting("show-straightfly", "Visualize Straightfly");
            EbCheckbox::drawSetting("show-wave-pattern", "Visualize Wave Pattern");
            EbCheckbox::drawSetting("show-ai", "Visualize AI");
            ImGui::TextDisabled("AI: purple frame = perception grid; dots = ranked delays.");
        }
        EbSectionCard::end();

        if (EbUiCapabilities::keybindUI()) {
            if (EbSectionCard::begin("visual-hotkeys", "Hotkeys"))
                EbKeybind::drawSetting("hotkey-trajectory", "Trajectory Viz");
            EbSectionCard::end();
        }

        if (EbSectionCard::begin("visual-save", "Save Feedback")) {
            EbCheckbox::drawSetting("save-feedback-sound", "Feedback Sound");
            EbCheckbox::drawSetting("save-feedback-color", "Feedback Color Flash");
            EbCheckbox::drawSetting("save-feedback-counter", "Feedback Counter");
            EbColorPicker::drawSetting("save-icon-color", "Save Icon Color");
        }
        EbSectionCard::end();
    }

    static void drawUI() {
        if (EbSectionCard::begin("ui-appearance", "Appearance and Contrast"))
            EbDesktopMenuTheme::contrastSettings();
        EbSectionCard::end();

        if (EbSectionCard::begin("ui-sidebar", "Sidebar Materials"))
            EbDesktopMenuSidebarMaterial::settings();
        EbSectionCard::end();

        if (EbSectionCard::begin("ui-background", "Game Background"))
            EbDesktopMenuBackground::settings();
        EbSectionCard::end();

        if (EbSectionCard::begin("ui-chrome", "Interface")) {
            char const* scales[] = {"75%", "100%", "125%", "150%"};
            EbDropdown::drawSetting("desktop-ui-scale", "UI Scale", scales, 4);

            EbCheckbox::drawSetting("show-overlay", "In-Level Overlay");
#if defined(EB_UI_WINDOWS)
            EbCheckbox::drawSetting("hide-from-stream", "Hide From Stream");
#endif
            EbCheckbox::drawSetting("show-pause-hub", "Pause EB Button");
            if (EbUiCapabilities::desktopLayout()) {
                EbCheckbox::drawSetting("desktop-classic-settings", "Classic Settings Menu");
            } else if (EbButton::draw("mobile-classic-settings", "Open Geode Settings", true,
                                      "Open the classic settings interface without changing your "
                                      "menu preference.")) {
                EbPauseActionBridge::openClassicSettings();
            }
        }
        EbSectionCard::end();

        if (EbUiCapabilities::keybindUI()) {
            if (EbSectionCard::begin("ui-hotkeys", "Hotkeys"))
                EbKeybind::drawSetting("hotkey-desktop-menu", "Desktop Menu");
            EbSectionCard::end();
        }

        if (EbSectionCard::begin("ui-notifications", "Notifications")) {
            EbCheckbox::drawSetting("lag-warning-toast", "Lag Warning");
        }
        EbSectionCard::end();
        if (EbUiCapabilities::expensiveMaterials()) {
            if (EbSectionCard::begin("ui-quality", "Rendering Quality")) {
                char const* quality[] = {"Low", "Balanced", "High"};
                EbDropdown::drawSetting("ui-render-quality", "Shader Quality", quality, 3);
                ImGui::TextWrapped(
                    "Quality controls shader resolution and blur samples. Background Render Scale "
                    "is an additional per-effect multiplier.");
            }
            EbSectionCard::end();
        }
        if (EbSectionCard::begin("ui-diag", "Diagnostics")) {
            EbCheckbox::drawSetting("debug-logging", "Debug Logging");
            EbCheckbox::drawSetting("perf-trace", "Perf Trace");
            EbCheckbox::drawSetting("lag-probe", "Lag Probe");
        }
        EbSectionCard::end();
    }

    static void drawDev() {
        EbDesktopMenuDevPanel::draw();
    }

private:
    EbDesktopMenuPanelsDraw() = delete;
};

void EbDesktopMenuPanels::draw(SettingsCategory category) {
    switch (category) {
    case SettingsCategory::Main:
        EbDesktopMenuPanelsDraw::drawMain();
        break;
    case SettingsCategory::Assist:
        EbDesktopMenuPanelsDraw::drawAssist();
        break;
    case SettingsCategory::Learn:
        EbDesktopMenuPanelsDraw::drawLearn();
        break;
    case SettingsCategory::AI:
        EbDesktopMenuPanelsDraw::drawAi();
        break;
    case SettingsCategory::Modes:
        EbDesktopMenuPanelsDraw::drawModes();
        break;
    case SettingsCategory::Flight:
        EbDesktopMenuPanelsDraw::drawFlight();
        break;
    case SettingsCategory::Visual:
        EbDesktopMenuPanelsDraw::drawVisual();
        break;
    case SettingsCategory::UI:
        EbDesktopMenuPanelsDraw::drawUI();
        break;
    case SettingsCategory::Dev:
        EbDesktopMenuPanelsDraw::drawDev();
        break;
    case SettingsCategory::Count:
        break;
    }
}

#else

void EbDesktopMenuPanels::draw(SettingsCategory category) {
    (void)category;
}

#endif
