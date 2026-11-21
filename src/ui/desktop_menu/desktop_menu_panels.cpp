#include "desktop_menu_panels.hpp"
#include "../../config/assist_policy_settings.hpp"
#include "../../config/stage_config.hpp"
#include "../../core/assist/assist_controller.hpp"
#include "../../core/assist/flight/flight_intent.hpp"
#include "../../core/assist/flight/wave_pattern_recognizer.hpp"
#include "../../core/learning/hybrid_learning.hpp"
#include "../../core/learning/level_profile.hpp"
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
        if (EbSectionCard::begin("ai-status", "Candidate Scorer")) {
            auto& neural = EbNeuralLearning::get();
            if (EbStageConfig::assistMode() != AssistMode::AI)
                ImGui::TextWrapped("Choose Assist Mode → AI on Main to score delays with the "
                                   "neural candidate scorer. Shadow physics still decides what "
                                   "may arm.");
            else if (!EbLevelProfile::get().loaded())
                ImGui::TextWrapped("Open a level to load or train per-level .neural weights.");
            else {
                bool frozen = neural.frozen();
                if (EbCheckbox::draw("freeze-ai", "Keep current weights", &frozen, true,
                                     "Infer with the loaded prior / level weights without "
                                     "updating them. Applies until you leave this level."))
                    neural.freeze(frozen);
                if (!neural.hasUsableWeights())
                    ImGui::TextWrapped(
                        "No usable weights yet. Play with Auto-Tune on to train, or load the "
                        "packaged prior below.");
                else if (frozen)
                    ImGui::TextWrapped(
                        "Weights are fixed for this visit. AI still ranks delays and verifies "
                        "them before arming.");
                else if (!EbStageConfig::autoTuneEnabled())
                    ImGui::TextWrapped(
                        "Auto-Tune is off: AI infers from loaded weights but does not train. "
                        "Turn Auto-Tune on (Learn tab) to update weights while you play.");
                else
                    ImGui::TextWrapped(
                        "Training on verified outcomes. AI ranks top-k delays (Hybrid mature "
                        "timings first when this level has .hybrid tables), verifies each with "
                        "Live physics, then arms the first safe delay — never from raw score.");
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
                ImGui::Text("Inferences this visit   %d", neural.inferences);
                ImGui::Text("Edges seen   %d", neural.edges);
                ImGui::Text("Weight updates   %d", neural.updates);
                ImGui::Text("Tried / confirmed   %d / %d", neural.tried, neural.confirmed);
                ImGui::Text("Local deaths   %d", neural.deaths);
                ImGui::Text("Replay size   %d", neural.replay().size());
                ImGui::Text("Session samples / attempts   %d / %d",
                            EbNeuralSession::get().size(), EbNeuralSession::get().attemptCount());
            }
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("ai-train", "Autoplay + Session Train")) {
            auto& neural = EbNeuralLearning::get();
            auto& autoplay = EbNeuralAutoplay::get();
            bool const aiOn = EbStageConfig::assistMode() == AssistMode::AI;
            bool autoplayOn = autoplay.enabled();
            if (EbCheckbox::draw("ai-autoplay", "Autoplay (RL self-play)", &autoplayOn, aiOn,
                                 "Synthesizes Jump presses through the normal AI assist path so "
                                 "the scorer can RL-train without a human. Requires Assist Mode → "
                                 "AI. Turn Auto-Tune on (Learn) to write weights online."))
                autoplay.setEnabled(autoplayOn);
            bool restart = autoplay.restartOnDeath();
            if (EbCheckbox::draw("ai-autoplay-restart", "Restart on Death", &restart, aiOn,
                                 "When Autoplay is on, reset the attempt shortly after death."))
                autoplay.setRestartOnDeath(restart);
            ImGui::Text("Autoplay presses / restarts   %d / %d", autoplay.presses(),
                        autoplay.restarts());
            ImGui::Separator();
            ImGui::TextWrapped(
                "Every attempt (you or Autoplay) stores scorer transitions. After a session, "
                "Train Session batch-updates the level .neural weights from that buffer "
                "(imitation + outcome RL).");
            if (EbButton::draw("ai-train-session", "Train Session",
                               aiOn && EbLevelProfile::get().loaded() &&
                                   EbNeuralSession::get().size() > 0,
                               "Run several passes of TD / rank / confidence updates from the "
                               "session buffer, then save .neural.")) {
                int const n = neural.trainSession(4);
                ImGui::Text("Trained %d updates", n);
            }
            if (EbButton::draw("ai-clear-session", "Clear Session",
                               EbNeuralSession::get().size() > 0 ||
                                   EbNeuralSession::get().attemptCount() > 0,
                               "Drop buffered session samples without touching weights."))
                EbNeuralSession::get().clear();
            if (EbNeuralSession::get().lastTrainUpdates() > 0)
                ImGui::TextDisabled("Last train: %d updates",
                                    EbNeuralSession::get().lastTrainUpdates());
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("ai-viz", "Visualization")) {
            EbCheckbox::drawSetting("show-ai", "Visualize AI");
            ImGui::TextWrapped(
                "Shows the local grid the scorer reads (solids / hazards / orbs), top-k delay "
                "markers by confidence, last armed delay, and a green pulse on weight updates. "
                "Same toggle as Visual → Visualize AI — draw only, never arms.");
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("ai-knobs", "Scorer Knobs")) {
            auto& neural = EbNeuralLearning::get();
            bool const aiOn = EbStageConfig::assistMode() == AssistMode::AI;
            EbCheckbox::draw("ai-prefer-first", "Verify AI First", &neural.preferAiFirst, aiOn,
                             "Rank delays with the scorer and verify top-k with Live physics "
                             "before ordinary Live search. Off = Live proposes first.");
            EbSlider::drawInt("ai-topk", "Top-K Verify", &neural.topK, 1, 8, "", aiOn,
                              "How many highest-scoring delays to verify before falling through "
                              "to Live. Lower is cheaper; higher explores more candidates.");
            EbSlider::drawFloat("ai-lr", "Learning Rate", &neural.learningRate, 0.001f, 0.05f, 3,
                                "", aiOn,
                                "Step size for on-play weight updates when Auto-Tune is on and "
                                "weights are not frozen.");
            EbSlider::drawInt("ai-observation", "Observation", &neural.observationTicks, 24, 96,
                              " ticks", aiOn,
                              "How long after an AI-armed input to attribute survival or death.");
            if (EbButton::draw("ai-load-prior", "Load Packaged Prior",
                               aiOn && EbLevelProfile::get().loaded(),
                               "Replace in-memory weights with the packaged prior.ebnn and write "
                               "them into this level's .neural file."))
                (void)neural.loadPrior();
            if (EbButton::draw("ai-reset-weights", "Reset AI Weights",
                               aiOn && EbLevelProfile::get().loaded(),
                               "Clear this level's .neural weights (Hybrid tables stay). Same as "
                               "the AI half of Reset This Level."))
                neural.reset();
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
