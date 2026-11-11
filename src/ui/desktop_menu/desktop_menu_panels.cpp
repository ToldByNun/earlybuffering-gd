#include "desktop_menu_panels.hpp"
#include "desktop_menu_icons.hpp"
#include "widgets/eb_button.hpp"
#include "widgets/eb_checkbox.hpp"
#include "widgets/eb_color_picker.hpp"
#include "widgets/eb_combo_box.hpp"
#include "widgets/eb_dropdown.hpp"
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
            EbCheckbox::drawSetting("adaptive-plan", "Adaptive Plan");
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
            EbSlider::drawFloatSetting("assist-off-1-from", "Range 1 From", -1.0f, 100.0f, 1, "%");
            EbSlider::drawFloatSetting("assist-off-1-to", "Range 1 To", -1.0f, 100.0f, 1, "%");
            gap();
            EbSlider::drawFloatSetting("assist-off-2-from", "Range 2 From", -1.0f, 100.0f, 1, "%");
            EbSlider::drawFloatSetting("assist-off-2-to", "Range 2 To", -1.0f, 100.0f, 1, "%");
            gap();
            EbSlider::drawFloatSetting("assist-off-3-from", "Range 3 From", -1.0f, 100.0f, 1, "%");
            EbSlider::drawFloatSetting("assist-off-3-to", "Range 3 To", -1.0f, 100.0f, 1, "%");
            gap();
            EbSlider::drawFloatSetting("assist-off-4-from", "Range 4 From", -1.0f, 100.0f, 1, "%");
            EbSlider::drawFloatSetting("assist-off-4-to", "Range 4 To", -1.0f, 100.0f, 1, "%");
            gap();
            EbSlider::drawFloatSetting("assist-off-5-from", "Range 5 From", -1.0f, 100.0f, 1, "%");
            EbSlider::drawFloatSetting("assist-off-5-to", "Range 5 To", -1.0f, 100.0f, 1, "%");
            gap();
            EbSlider::drawFloatSetting("assist-off-6-from", "Range 6 From", -1.0f, 100.0f, 1, "%");
            EbSlider::drawFloatSetting("assist-off-6-to", "Range 6 To", -1.0f, 100.0f, 1, "%");
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
            EbButton::drawAction("reset-learn-action", "Reset Learning");
            gap();
            EbButton::drawAction("export-config-action", "Export Config");
            gap();
            EbButton::drawAction("import-config-action", "Import Config");
            gap();
            EbButton::drawAction("browse-cloud-configs-action", "Cloud Configs");
        }
        EbSectionCard::end();
    }

    static void drawBake() {
        if (EbSectionCard::begin("bake-actions", "Bake Actions")) {
            EbButton::drawAction("import-bake-replay", "Import Replay");
            gap();
            EbButton::drawAction("cycle-assist-mode", "Cycle Mode");
            gap();
            EbButton::drawAction("bake-map-action", "Bake Map");
            gap();
            EbButton::drawAction("brute-bake-action", "Brute Bake");
            gap();
            EbButton::drawAction("cancel-bake-action", "Cancel Bake");
            gap();
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
        std::snprintf(enableKey, sizeof(enableKey), "%s-enable", prefix);
        std::snprintf(overwriteKey, sizeof(overwriteKey), "%s-overwrite", prefix);
        std::snprintf(ticksKey, sizeof(ticksKey), "%s-ticks", prefix);
        std::snprintf(capKey, sizeof(capKey), "%s-cap", prefix);
        std::snprintf(slackKey, sizeof(slackKey), "%s-slack", prefix);
        std::snprintf(latencyKey, sizeof(latencyKey), "%s-latency", prefix);

        EbCheckbox::drawSetting(enableKey, "Enable");
        EbCheckbox::drawSetting(overwriteKey, "Overwrite");
        EbSlider::drawIntSetting(ticksKey, "Window", 0, 32);
        EbSlider::drawIntSetting(capKey, "Look-Ahead", 0, 240);
        EbSlider::drawIntSetting(slackKey, "Slack", -1, 16);
        EbSlider::drawFloatSetting(latencyKey, "Latency", 0.0f, 2.0f, 2);
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
        if (EbSectionCard::begin("dev-session", "Session")) {
            EbCheckbox::drawSetting("dev-record-enable", "Record Enable");
            EbSlider::drawIntSetting("dev-record-count", "Record Count", 1, 50);
            EbCheckbox::drawSetting("dev-count-empty-attempts", "Count Empty Attempts");
            EbCheckbox::drawSetting("dev-freeze-world", "Freeze World");
            EbCheckbox::drawSetting("dev-disable-random", "Disable Random");
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("dev-eval", "Eval")) {
            EbCheckbox::drawSetting("dev-eval-on-death", "Eval On Death");
            {
                char const* gridOpts[] = {"full", "band", "sparse"};
                char const* gridLabels[] = {"Full", "Band", "Sparse"};
                EbDropdown::drawSetting("dev-eval-grid", "Eval Grid", gridOpts, 3, gridLabels);
            }
            EbSlider::drawIntSetting("dev-eval-horizon", "Eval Horizon", 1, 320);
            EbSlider::drawIntSetting("dev-eval-tail", "Eval Tail", 0, 320);
            {
                char const* objOpts[] = {"survive_then_endx_v1", "planner_mirror_v1",
                                         "horizon_clear_v1"};
                char const* objLabels[] = {"Survive then EndX", "Planner Mirror", "Horizon Clear"};
                EbDropdown::drawSetting("dev-objective", "Objective", objOpts, 3, objLabels);
            }
            EbCheckbox::drawSetting("dev-eval-require-deps", "Require Deps");
            EbCheckbox::drawSetting("dev-record-cands", "Record Candidates");
            EbSlider::drawIntSetting("dev-eval-tick-budget", "Eval Tick Budget", 500, 50000);
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("dev-overlay", "Overlay")) {
            EbCheckbox::drawSetting("dev-overlay", "Dev Overlay");
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("dev-actions", "Actions")) {
            EbButton::drawAction("dev-start-record-action", "Start Recording");
            gap();
            EbButton::drawAction("dev-stop-record-action", "Stop Recording");
            gap();
            EbButton::drawAction("dev-eval-last-action", "Eval Last Attempt");
            gap();
            EbButton::drawAction("dev-eval-session-action", "Eval Session");
            gap();
            EbButton::drawAction("dev-export-session-action", "Export Session");
            gap();
            EbButton::drawAction("dev-clear-traces-action", "Clear Traces");
            gap();
            EbButton::drawAction("dev-copy-report-action", "Copy Last Report");
        }
        EbSectionCard::end();

        if (EbSectionCard::begin("dev-diag", "Diagnostics")) {
            EbButton::drawAction("dev-dump-diag-action", "Dump All Diag");
        }
        EbSectionCard::end();
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
