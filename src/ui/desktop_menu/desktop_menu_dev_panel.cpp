#include "desktop_menu_dev_panel.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "../../core/sim/validation/native_isolation_probe.hpp"
#include "../../dev/capture/dev_trace_store.hpp"
#include "../../dev/dev_config.hpp"
#include "../../dev/dev_session.hpp"
#include "../../dev/evaluation/dev_state_diff.hpp"
#include "../../dev/reporting/dev_report.hpp"
#include "../settings/action_button_setting.hpp"
#include "desktop_menu_alert.hpp"
#include "desktop_menu_bind.hpp"
#include "desktop_menu_theme.hpp"
#include "widgets/eb_button.hpp"
#include "widgets/eb_checkbox.hpp"
#include "widgets/eb_dropdown.hpp"
#include "widgets/eb_section_card.hpp"
#include "widgets/eb_slider.hpp"
#include <Geode/Geode.hpp>
#include <filesystem>
#include <fmt/format.h>
#include <imgui.h>
#include <string>

class EbDesktopMenuDevPanelDraw {
public:
    static void gap() {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
    }

    static void smallGap() {
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    static void mutedText(std::string const& text) {
        if (text.empty())
            return;
        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::muted());
        ImGui::TextWrapped("%s", text.c_str());
        ImGui::PopStyleColor();
    }

    static bool researchPresetReady() {
        return EbDesktopMenuBind::getBool("dev-record-enable") &&
               EbDesktopMenuBind::getBool("dev-record-cands") &&
               EbDesktopMenuBind::getBool("dev-freeze-world") &&
               EbDesktopMenuBind::getBool("dev-record-hazards") &&
               EbDesktopMenuBind::getBool("dev-eval-on-death") &&
               EbDesktopMenuBind::getBool("dev-overlay") &&
               EbDesktopMenuBind::getBool("dev-disable-random");
    }

    static void applyResearchPreset() {
        (void)EbDesktopMenuBind::setBool("dev-record-enable", true);
        (void)EbDesktopMenuBind::setBool("dev-record-cands", true);
        (void)EbDesktopMenuBind::setBool("dev-freeze-world", true);
        (void)EbDesktopMenuBind::setBool("dev-record-hazards", true);
        (void)EbDesktopMenuBind::setBool("dev-eval-on-death", true);
        (void)EbDesktopMenuBind::setBool("dev-overlay", true);
        (void)EbDesktopMenuBind::setBool("dev-disable-random", true);
        EbDesktopAlert::info("EarlyBuffer Dev",
                             "Research Preset on:\n"
                             "Record + Candidates + Freeze World + Hazards +\n"
                             "Eval On Death + Overlay + Disable Random.\n\n"
                             "Next: press Record, die a few times, then Export Research Pack.");
    }

    static std::string shortPath(std::filesystem::path const& path) {
        if (path.empty())
            return {};
        auto const text = path.string();
        if (text.size() <= 56)
            return text;
        return std::string("…") + text.substr(text.size() - 52);
    }

    static void fireAction(char const* key) {
        if (key)
            EbActionButtonSettingImplementation::fireSettingAction(key);
    }

    static void drawStatus() {
        if (!EbSectionCard::begin("dev-status", "Now")) {
            EbSectionCard::end();
            return;
        }

        auto& session = EbDevSession::get();
        auto& store = EbDevTraceStore::get();
        bool const recording = session.isRecording() && store.isOpen();

        ImGui::PushStyleColor(ImGuiCol_Text, recording ? EbDesktopMenuTheme::accentLight()
                                                       : EbDesktopMenuTheme::text());
        ImGui::TextUnformatted(session.statusLine().c_str());
        ImGui::PopStyleColor();

        if (recording || store.isOpen()) {
            auto const& file = store.session();
            mutedText(fmt::format("{} · {}", file.sessionId,
                                  file.levelName.empty() ? file.levelKey : file.levelName));
            auto const path = store.sessionPath();
            if (!path.empty())
                mutedText(shortPath(path));
        } else {
            mutedText("Idle — Setup, then Record.");
        }

        if (EbDevReport::hasLast()) {
            auto const& report = EbDevReport::last();
            std::string const line = report.overlayLine.empty()
                                         ? EbDevReport::formatOverlayLine(report)
                                         : report.overlayLine;
            smallGap();
            ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::text());
            ImGui::TextWrapped("%s", line.c_str());
            ImGui::PopStyleColor();
        } else {
            smallGap();
            mutedText("No report yet.");
        }

        EbSectionCard::end();
    }

    static void drawSetup() {
        if (!EbSectionCard::begin("dev-setup", "Setup")) {
            EbSectionCard::end();
            return;
        }

        bool const ready = researchPresetReady();
        if (EbButton::draw("##dev-research-preset",
                           ready ? "Research Preset ✓" : "Apply Research Preset", true,
                           "Record Enable, Candidates, Freeze World, Hazards, Eval On Death, "
                           "Overlay, Disable Random.",
                           !ready)) {
            applyResearchPreset();
        }

        gap();
        EbCheckbox::drawSetting("dev-record-enable", "Record Enable");
        EbCheckbox::drawSetting("dev-eval-on-death", "Eval On Death");
        EbCheckbox::drawSetting("dev-overlay", "Dev Overlay");
        EbSlider::drawIntSetting("dev-record-count", "Keep last N attempts", 1, 50);

        EbSectionCard::end();
    }

    static void drawSetupExtras() {
        if (!EbSectionCard::begin("dev-setup-extras", "Setup Extras")) {
            EbSectionCard::end();
            return;
        }

        EbCheckbox::drawSetting("dev-freeze-world", "Freeze World");
        EbCheckbox::drawSetting("dev-record-hazards", "Record Hazards / Hitbox");
        EbCheckbox::drawSetting("dev-record-traj", "Record Trajectories (heavy)");
        EbCheckbox::drawSetting("dev-record-cands", "Record Candidates");
        EbCheckbox::drawSetting("dev-disable-random", "Disable Random");
        EbCheckbox::drawSetting("dev-count-empty-attempts", "Count Empty Attempts");

        EbSectionCard::end();
    }

    static void drawRecord() {
        if (!EbSectionCard::begin("dev-record", "Record")) {
            EbSectionCard::end();
            return;
        }

        bool const recording = EbDevSession::get().isRecording();
        bool const canStart = EbDevConfig::recordEnable();

        if (recording) {
            mutedText("Recording. Die / complete attempts — Eval On Death updates the report.");
            gap();
            if (EbButton::draw("##dev-stop", "Stop Recording", true,
                               "Seals the session JSON under devtraces/.", true)) {
                fireAction("dev-stop-record-action");
            }
        } else {
            mutedText(canStart
                          ? "Start in a level. With Eval On Death you usually skip manual eval."
                          : "Turn on Record Enable (or Apply Research Preset) first.");
            gap();
            if (EbButton::draw("##dev-start", "Start Recording", canStart,
                               "Starts a rolling Dev Lab session in the current level.", true)) {
                fireAction("dev-start-record-action");
            }
        }

        EbSectionCard::end();
    }

    static void drawEval() {
        if (!EbSectionCard::begin("dev-eval", "Eval")) {
            EbSectionCard::end();
            return;
        }

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
        EbSlider::drawIntSetting("dev-eval-tick-budget", "Eval Tick Budget", 500, 50000);

        gap();
        EbButton::beginRow(2);
        if (EbButton::draw("##dev-eval-last", "Eval Last", true,
                           "Counterfactual grid on the latest planFinished witness.")) {
            fireAction("dev-eval-last-action");
        }
        if (EbButton::draw("##dev-eval-session", "Eval Session", true,
                           "Eval every attempt in the open session with a plan witness.")) {
            fireAction("dev-eval-session-action");
        }
        EbButton::endRow();

        EbSectionCard::end();
    }

    static void drawSave() {
        if (!EbSectionCard::begin("dev-save", "Save")) {
            EbSectionCard::end();
            return;
        }

        mutedText(
            "One click: eval all → write pack folder → path on clipboard (send that folder).");
        gap();

        if (EbButton::draw("##dev-research-pack", "Export Research Pack", true,
                           "Eval Session/Last, then research/research_<stamp>/ (session, report, "
                           "fingerprint, config). Copies the folder path.",
                           true)) {
            fireAction("dev-export-research-pack-action");
        }

        gap();
        bool const hasReport = EbDevReport::hasLast();
        EbButton::beginRow(2);
        if (EbButton::draw("##dev-copy-report", "Copy Report", hasReport,
                           "Copies the last fid/bestSurv line (+ save path) to the clipboard.")) {
            fireAction("dev-copy-report-action");
        }
        if (EbButton::draw("##dev-session-path", "Session Path", true,
                           "Shows / copies the current session JSON path.")) {
            fireAction("dev-export-session-action");
        }
        EbButton::endRow();

        gap();
        if (EbButton::draw("##dev-native-isolation", "Native Isolation", true,
                           "Main menu only: compare two native fixture worlds serially.")) {
            EbDesktopAlert::info("Native Isolation", EbNativeIsolationProbe::run().c_str());
        }
        gap();
        if (EbButton::draw("##dev-dump-diag", "Dump Diag", true,
                           "Bake/TW/clock folder — for bake bugs, not Live Delay research.")) {
            fireAction("dev-dump-diag-action");
        }

        EbSectionCard::end();
    }

    static void drawCompare() {
        if (!EbSectionCard::begin("dev-compare", "Compare")) {
            EbSectionCard::end();
            return;
        }

        mutedText("Newest sealed session vs median of the older ones → gained / flat / regressed.");
        gap();
        EbSlider::drawIntSetting("dev-compare-last-n", "Compare Last N", 2, 20);
        EbCheckbox::drawSetting("dev-compare-same-level", "Same levelKey only");
        gap();
        if (EbButton::draw("##dev-compare-sessions", "Compare Sessions", true,
                           "Writes compare_<stamp>.json under devtraces/ and copies the path.",
                           true)) {
            fireAction("dev-compare-sessions-action");
        }

        EbSectionCard::end();
    }

    static void drawDanger() {
        if (!EbSectionCard::begin("dev-danger", "Danger")) {
            EbSectionCard::end();
            return;
        }

        mutedText("Deletes session-*.json under devtraces/ only.");
        gap();
        if (EbButton::draw("##dev-clear", "Clear Traces…", true,
                           "Stops recording if needed, then deletes Dev session files.")) {
            fireAction("dev-clear-traces-action");
        }

        EbSectionCard::end();
    }

    static void draw() {
        drawStatus();
        gap();
        drawSetup();
        gap();
        drawSetupExtras();
        gap();
        drawRecord();
        gap();
        drawEval();
        gap();
        drawSave();
        gap();
        drawCompare();
        gap();
        drawDanger();
    }

private:
    EbDesktopMenuDevPanelDraw() = delete;
};

void EbDesktopMenuDevPanel::draw() {
    EbDesktopMenuDevPanelDraw::draw();
}

#else

void EbDesktopMenuDevPanel::draw() {}

#endif
