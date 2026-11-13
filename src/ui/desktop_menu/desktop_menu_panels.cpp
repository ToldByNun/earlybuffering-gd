#include "desktop_menu_panels.hpp"
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

#include <imgui.h>
#include <cstdio>

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
                char const* opts[] = {"Live", "Replay", "Hybrid", "BruteForce"};
                EbComboBox::drawSetting("assist-mode", "Assist Mode", opts, 4);
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
            {
                char const* pickOpts[] = {"v1", "v2"};
                char const* pickLabels[] = {"Pick v1", "Pick v2"};
                EbDropdown::drawSetting("assist-pick", "Pick Mode", pickOpts, 2, pickLabels);
            }
            {
                char const* engOpts[] = {"ShadowPlayer", "Trajectory"};
                EbComboBox::drawSetting("assist-engine", "Assist Engine", engOpts, 2);
            }
            EbSlider::drawIntSetting("cps-limit", "CPS Limit", 0, 40);
            EbSlider::drawIntSetting("min-click-interval-ms", "Min Click Interval", 0, 500, "ms");
            EbCheckbox::drawSetting("assist-orbs", "Orb Assist");
            EbSlider::drawIntSetting("sim-budget-us", "Sim Budget", 0, 10000, "µs");
            EbSlider::drawIntSetting("tick-budget", "Plan Tick Budget", 500, 20000);
            EbCheckbox::drawSetting("assist-press", "Assist Presses");
            EbCheckbox::drawSetting("assist-release", "Assist Releases");
        }
        EbSectionCard::end();
    }

    static void drawLearn() {
        if (EbSectionCard::begin("learn-toggles", "Learning")) {
            EbCheckbox::drawSetting("auto-tune", "Auto-Tune Per Level");
            EbCheckbox::drawSetting("self-bake", "Self-Bake (Practice)");
            EbCheckbox::drawSetting("self-bake-normal", "Self-Bake in Normal");
            EbCheckbox::drawSetting("self-bake-refine", "Self-Bake Refine");
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("learn-actions", "Config")) {
            // No gap() between rows — ItemSpacing alone keeps H/V grid even.
            EbButton::beginRow(2);
            EbButton::drawAction("export-config-action", "Export Config");
            EbButton::drawAction("import-config-action", "Import Config");
            EbButton::endRow();
            EbButton::beginRow(2);
            EbButton::drawAction("reset-learn-action", "Reset Learning");
            EbButton::drawAction("browse-cloud-configs-action", "Cloud Configs");
            EbButton::endRow();
        }
        EbSectionCard::end();
    }

    static void drawBake() {
        if (EbSectionCard::begin("bake-actions", "Bake Actions")) {
            EbButton::beginRow(2);
            EbButton::drawAction("import-bake-replay", "Import Replay");
            EbButton::drawAction("cycle-assist-mode", "Cycle Mode");
            EbButton::endRow();
            EbButton::beginRow(2);
            EbButton::drawAction("bake-map-action", "Bake Map");
            EbButton::drawAction("brute-bake-action", "Brute Bake");
            EbButton::endRow();
            EbButton::drawAction("cancel-bake-action", "Cancel Bake");
            EbSlider::drawIntSetting("bake-radius", "Bake Radius", 0, 128);
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("bake-triggers", "Trigger World")) {
            EbCheckbox::drawSetting("trigger-bake", "Trigger World Bake");
            EbCheckbox::drawSetting("trigger-bake-skip-dynamic", "Skip Dynamic Triggers");
            {
                char const* verifyOpts[] = {"off", "ab"};
                char const* verifyLabels[] = {"Off", "A/B"};
                EbDropdown::drawSetting("assist-verify", "Assist Verify", verifyOpts, 2,
                                        verifyLabels);
            }
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
            EbCheckbox::drawSetting("show-bake-path", "Bake Path Visualizer");
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
        if (EbSectionCard::begin("ui-chrome", "Interface")) {
            EbColorPicker::drawSetting("desktop-accent-color", "Accent Color");
            gap();
            EbCheckbox::drawSetting("show-overlay", "In-Level Overlay");
            EbCheckbox::drawSetting("show-pause-hub", "Pause EB Button");
            EbCheckbox::drawSetting("show-pause-bake", "Pause Status Lines");
            EbCheckbox::drawSetting("desktop-classic-settings", "Classic Settings Menu");
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
    case SettingsCategory::Bake:
        EbDesktopMenuPanelsDraw::drawBake();
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
