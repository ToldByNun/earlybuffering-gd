#include "desktop_menu_dev_panel.hpp"
#include "../../core/assist/assist_controller.hpp"
#include "../../core/assist/flight/wave_pattern_recognizer.hpp"
#include "../../core/learning/hybrid_learning.hpp"

#if defined(EB_DESKTOP_IMGUI)

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
        EbSlider::drawIntSetting("dev-eval-tick-budget", "Eval Tick Budget", 500, 50000, "", true,
                                 100);

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
        if (EbSectionCard::begin("dev-learning", "Learned Timings")) {
            auto const rows = EbHybridLearning::get().inspect();
            ImGui::TextWrapped("Each bar shows an action's recent weight. Repeated survival "
                               "supports immediate input. Death lowers its weight. Suggestions "
                               "still require current physics verification.");
            // The shell routes the captured Cocos wheel directly to the hovered child.
            ImGui::BeginChild("learning-weight-list", ImVec2(0, 250), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoScrollWithMouse);
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()), 58.f);
            while (clipper.Step())
                for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                    auto const& row = rows[index];
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    float width = ImGui::GetContentRegionAvail().x;
                    auto* draw = ImGui::GetWindowDrawList();
                    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + 52),
                                        IM_COL32(35, 38, 52, 255), 7.f);
                    draw->AddRectFilled(
                        ImVec2(pos.x + 10, pos.y + 39),
                        ImVec2(pos.x + 10 +
                                   (width - 20) * std::clamp((row.weight + 64.f) / 256.f, 0.f, 1.f),
                               pos.y + 44),
                        row.weight < 0 ? IM_COL32(210, 100, 110, 255)
                                       : IM_COL32(105, 140, 215, 255),
                        2.f);
                    ImGui::SetCursorScreenPos(ImVec2(pos.x + 10, pos.y + 5));
                    ImGui::Text("Delay %d   Weight %d   Samples %d", row.delay, row.weight,
                                row.samples);
                    ImGui::SetCursorScreenPos(ImVec2(pos.x + 10, pos.y + 22));
                    ImGui::TextDisabled("%llx  %s", static_cast<unsigned long long>(row.key),
                                        row.selected ? "Ready to propose"
                                        : row.samples < EbHybridLearning::get().minimumAttempts
                                            ? "Needs more attempts"
                                            : "Insufficient weight or competing action");
                    ImGui::SetCursorScreenPos(pos);
                    ImGui::Dummy(ImVec2(width, 58.f - ImGui::GetStyle().ItemSpacing.y));
                    if (ImGui::IsItemHovered()) {
                        auto const& e = row.evidence;
                        ImGui::SetTooltip(
                            "Survived %d   Local deaths %d   Unclear %d\nRescue evidence %d   "
                            "Verified harm %d\nLast death: +%d ticks, %d intervening inputs\nInput "
                            "phase bin %d   Sequence %d (-1 = unavailable)",
                            e.successCount, e.failureCount, e.unknownCount, e.verifiedRescueCount,
                            e.verifiedHarmCount, e.deathDeltaTicks, e.interveningInputs,
                            e.timingPhase, e.inputSequenceSignature);
                    }
                }
            ImGui::EndChild();
        }
        EbSectionCard::end();
        if (EbSectionCard::begin("dev-flight-measurement", "Flight Measurement")) {
            bool shipLineEnabled = EbAssistController::get().shipLineEnabled();
            if (EbCheckbox::draw(
                    "ship-line-enable", "Ship line assistance (session)", &shipLineEnabled, true,
                    "Stabilizes an acquired Ship line by delaying real inputs (line first, ordinary "
                    "safe as fallback). Requires press + release assist. Solo Ship only. Visualize "
                    "Straightfly also arms this while Tracking. Modes → Ship may stay off. Delay "
                    "window ≥20 ticks. Session-only — not saved."))
                EbAssistController::get().setShipLineEnabled(shipLineEnabled);
            static bool manualTarget = false;
            static float targetAngle = 0.f;
            bool changed = EbCheckbox::draw(
                "flight-manual-target", "Manual target (preview)", &manualTarget, true,
                "Uses this travel-relative angle for intent diagnostics. Steering arrives in the "
                "next implementation stage. Resets acquisition when changed.");
            changed |= EbSlider::drawFloat("flight-target-angle", "Target angle", &targetAngle,
                                           -80.f, 80.f, 1, " deg", manualTarget,
                                           "Positive means upward transverse motion relative to "
                                           "travel. This preview is session-only.");
            if (changed)
                EbAssistController::get().configureFlightIntent(manualTarget, targetAngle);
            for (int player = 0; player < 2; ++player) {
                auto const intent = EbAssistController::get().flightIntent(player);
                ImGui::Text("P%d intent: %s   confidence %.0f%%", player + 1,
                            EbFlightIntent::stateName(intent.state), intent.confidence * 100.);
                if (intent.targetValid)
                    ImGui::Text("Target %.1f deg   Hold %.0f ms / Release %.0f ms",
                                intent.targetDegrees, intent.holdSeconds * 1000.,
                                intent.releaseSeconds * 1000.);
                auto const wave = EbAssistController::get().wavePattern(player);
                ImGui::Text("P%d wave: %s   confidence %.0f%%", player + 1,
                            EbWavePatternRecognizer::stateName(wave.state), wave.confidence * 100.);
                if (wave.targetValid)
                    ImGui::Text("Hold %.0f ms / Release %.0f ms   duty %.0f%%   motif %dp  "
                                "travel %.1f deg   jitter %.0f ms",
                                wave.holdSeconds * 1000., wave.releaseSeconds * 1000.,
                                wave.dutyCycle * 100., wave.motifPairs, wave.averageTravelDegrees,
                                wave.jitterSeconds * 1000.);
                auto const result = EbAssistController::get().flightMeasurement(player);
                ImGui::Text("Player %d: %s", player + 1,
                            result.headingValid ? "Measured" : "Acquiring / unavailable");
                if (result.headingValid) {
                    ImGui::Text("World %.1f deg   Travel %.1f deg   Filtered %.1f deg",
                                result.worldAngle, result.travelAngle, result.smoothedAngle);
                    ImGui::Text("Velocity %.1f / %.1f units/s   Variance %.1f", result.vx,
                                result.vy, result.velocityVariance);
                }
            }
            ImGui::TextWrapped(
                "Ship intent locks a straight-flight line. Wave pattern recognition (AP07) locks "
                "hold/release spam and short motifs — corrections arrive in AP08.");
        }
        EbSectionCard::end();
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
