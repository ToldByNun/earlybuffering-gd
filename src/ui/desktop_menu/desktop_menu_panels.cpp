#include "desktop_menu_panels.hpp"
#include "desktop_menu_background.hpp"
#include "desktop_menu_sidebar_material.hpp"
#include "../../config/assist_policy_settings.hpp"
#include "../../core/learning/hybrid_learning.hpp"
#include "../../core/learning/level_profile.hpp"
#include "desktop_menu_bind.hpp"
#include "desktop_menu_dev_panel.hpp"
#include "desktop_menu_icons.hpp"
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
                char const* opts[] = {"Live", "Hybrid"};
                EbComboBox::drawSetting("assist-mode", "Assist Mode", opts, 2);
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
            EbSlider::drawIntSetting("sim-budget-us", "Sim Budget", 0, 10000, "µs");
            EbSlider::drawIntSetting("tick-budget", "Plan Tick Budget", 500, 20000);
            EbCheckbox::drawSetting("assist-press", "Assist Presses");
            EbCheckbox::drawSetting("assist-release", "Assist Releases");
        }
        EbSectionCard::end();
        if (EbSectionCard::begin("assist-trigger-world", "Trigger World")) {
            EbCheckbox::drawSetting("trigger-bake", "Prepare Trigger World");
            EbCheckbox::drawSetting("trigger-bake-skip-dynamic", "Skip Dynamic Triggers");
        }
        EbSectionCard::end();
    }

    static void drawLearn() {
        if (EbSectionCard::begin("learn-toggles", "Learning")) {
            EbCheckbox::drawSetting("auto-tune", "Auto-Tune Per Level");
            auto& learning = EbHybridLearning::get();
            if (!EbLevelProfile::get().loaded())
                ImGui::TextWrapped("Open a level to see its learning progress.");
            else if (EbStageConfig::assistMode() != AssistMode::Hybrid)
                ImGui::TextWrapped("Live uses its own timing adjustments. Choose Hybrid to learn "
                                   "which timings work on repeated attempts.");
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
            EbCheckbox::draw("learn-outcomes", "Track outcome counts", &learning.trackOutcomes);
            EbCheckbox::draw("learn-phase", "Record death timing phase", &learning.trackPhase,
                             learning.trackOutcomes);
            EbCheckbox::draw("learn-sequence", "Record recent input sequence",
                             &learning.trackSequence, learning.trackOutcomes);
            EbSlider::drawInt("learn-death-penalty", "Local death penalty", &learning.deathPenalty,
                              0, 4);
            EbSlider::drawInt("learn-survival-reward", "Survival reward", &learning.survivalReward,
                              1, 3);
            EbSlider::drawInt("learn-lineup-depth", "Lineup Look-Ahead", &learning.lineupDepth, 1,
                              3, " inputs", true,
                              "On familiar presses, check up to this many inputs ahead within the "
                              "current planning budget.");
            EbSlider::drawInt("learn-bin-x", "Position X resolution", &learning.positionBinX, 16,
                              256);
            EbSlider::drawInt("learn-bin-y", "Position Y resolution", &learning.positionBinY, 16,
                              256);
            EbSlider::drawInt("learn-tps-quantum", "TPS grouping", &learning.tpsQuantum, 1, 120);
            EbCheckbox::draw("learn-match-substep", "Distinguish sub-step inputs",
                             &learning.matchSubstep);
            EbCheckbox::draw("learn-match-intervals", "Distinguish input intervals",
                             &learning.matchIntervals);
            EbCheckbox::draw("learn-coarse", "Continuous mode fallback", &learning.coarseFallback);
            EbSlider::drawInt("learn-coarse-attempts", "Fallback independent attempts",
                              &learning.coarseAttempts, 3, 12, "", learning.coarseFallback);
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

    static void drawVisual() {
        if (EbSectionCard::begin("visual-viz", "Visualizers")) {
            EbCheckbox::drawSetting("show-trajectory", "Trajectory Visualizer");
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("visual-save", "Save Feedback")) {
            EbCheckbox::drawSetting("save-feedback-sound", "Feedback Sound");
            EbCheckbox::drawSetting("save-feedback-color", "Feedback Color Flash");
            EbCheckbox::drawSetting("save-feedback-counter", "Feedback Counter");
            EbColorPicker::drawSetting("save-icon-color", "Save Icon Color");
        }
        EbSectionCard::end();
    }

    static void drawUI() {
        if (EbSectionCard::begin("ui-sidebar", "Sidebar Materials"))
            EbDesktopMenuSidebarMaterial::settings();
        EbSectionCard::end();

        if (EbSectionCard::begin("ui-background", "Game Background"))
            EbDesktopMenuBackground::settings();
        EbSectionCard::end();

        if (EbSectionCard::begin("ui-chrome", "Interface")) {
            EbColorPicker::drawSetting("desktop-accent-color", "Accent Color");
            gap();
            EbCheckbox::drawSetting("show-overlay", "In-Level Overlay");
            EbCheckbox::drawSetting("show-pause-hub", "Pause EB Button");
            EbCheckbox::drawSetting("desktop-classic-settings", "Classic Settings Menu");
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("ui-notifications", "Notifications")) {
            EbCheckbox::drawSetting("lag-warning-toast", "Lag Warning");
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("ui-diag", "Diagnostics")) {
            EbCheckbox::drawSetting("debug-logging", "Debug Logging");
            EbCheckbox::drawSetting("perf-trace", "Perf Trace");
            EbCheckbox::drawSetting("lag-probe", "Lag Probe");
        }
        EbSectionCard::end();
    }

    static void drawKeys() {
        if (EbSectionCard::begin("keys-hotkeys", "Hotkeys")) {
            EbKeybind::drawSetting("hotkey-desktop-menu", "Desktop Menu");
            EbKeybind::drawSetting("hotkey-debug", "Debug Logging");
            EbKeybind::drawSetting("hotkey-reset-level", "Reset Level Learning");
            EbKeybind::drawSetting("hotkey-auto-tune", "Auto-Tune");
            EbKeybind::drawSetting("hotkey-assist-mode", "Cycle Assist Mode");
            EbKeybind::drawSetting("hotkey-trajectory", "Trajectory Viz");
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
    case SettingsCategory::Modes:
        EbDesktopMenuPanelsDraw::drawModes();
        break;
    case SettingsCategory::Visual:
        EbDesktopMenuPanelsDraw::drawVisual();
        break;
    case SettingsCategory::UI:
        EbDesktopMenuPanelsDraw::drawUI();
        break;
    case SettingsCategory::Keys:
        EbDesktopMenuPanelsDraw::drawKeys();
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
