#include "desktop_menu_shell.hpp"
#include "desktop_menu_fonts.hpp"
#include "desktop_menu_sidebar_smoke.hpp"
#include "desktop_menu_theme.hpp"
#include "widgets/eb_button.hpp"
#include "widgets/eb_checkbox.hpp"
#include "widgets/eb_color_picker.hpp"
#include "widgets/eb_combo_box.hpp"
#include "widgets/eb_dropdown.hpp"
#include "widgets/eb_keybind.hpp"
#include "widgets/eb_section_card.hpp"
#include "widgets/eb_slider.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include <Geode/Geode.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

class EbDesktopMenuShellState {
public:
    static SettingsCategory& active() {
        static SettingsCategory category = SettingsCategory::Main;
        return category;
    }

    static float& wheelDelta() {
        static float delta = 0.0f;
        return delta;
    }

    static bool& wantOpen() {
        static bool value = false;
        return value;
    }

    // Displayed open amount (0..1). Exponentially approaches wantOpen — reversing
    // mid-animation stays continuous (no ease hitch / pos snap).
    static float& openVisual() {
        static float value = 0.0f;
        return value;
    }

    // 1 = settled tab content. Dips on switch, eases back — interrupt-safe.
    static float& tabVisual() {
        static float value = 1.0f;
        return value;
    }

    static int& tabDir() {
        static int value = 1;
        return value;
    }

    static bool& tabResetScroll() {
        static bool value = false;
        return value;
    }

    static constexpr float kSidebarWidth = 196.0f;
    static constexpr float kMenuWidth = 900.0f;
    static constexpr float kMenuHeight = 560.0f;
    // Inset so section cards sit inside the tab — bg shows around the card.
    static constexpr float kContentPadX = 28.0f;
    static constexpr float kContentPadY = 24.0f;

    static constexpr float kOpenTau = 0.11f;   // ~settle in ~0.35s
    static constexpr float kCloseTau = 0.09f;  // ~settle in ~0.28s
    static constexpr float kTabTau = 0.10f;    // ~settle in ~0.30s
    static constexpr float kOpenSlidePx = 14.0f;
    static constexpr float kTabSlidePx = 16.0f;
    static constexpr float kTabDip = 0.62f; // don't hard-reset to 0 on re-click

private:
    EbDesktopMenuShellState() = delete;
};

class EbDesktopMenuShellAnim {
public:
    static float clampf(float value, float lo, float hi) {
        if (value < lo) {
            return lo;
        }
        if (value > hi) {
            return hi;
        }
        return value;
    }

    static float approach(float current, float target, float tau, float deltaTime) {
        if (tau <= 0.0001f) {
            return target;
        }
        float const dt = clampf(deltaTime, 0.0f, 1.0f / 20.0f);
        float const blend = 1.0f - std::exp(-dt / tau);
        float const next = current + (target - current) * blend;
        if (std::fabs(target - next) < 0.0008f) {
            return target;
        }
        return next;
    }

    static void updateOpen(float deltaTime) {
        float const target = EbDesktopMenuShellState::wantOpen() ? 1.0f : 0.0f;
        float const tau = EbDesktopMenuShellState::wantOpen() ? EbDesktopMenuShellState::kOpenTau
                                                             : EbDesktopMenuShellState::kCloseTau;
        float& visual = EbDesktopMenuShellState::openVisual();
        visual = approach(visual, target, tau, deltaTime);
        // Exponential ease never quite hits 0 — without a finish snap the shell
        // lingered nearly-invisible for a beat before ImGui finally hid.
        if (!EbDesktopMenuShellState::wantOpen() && visual < 0.04f) {
            visual = 0.0f;
        }
        if (EbDesktopMenuShellState::wantOpen() && visual > 0.985f) {
            visual = 1.0f;
        }
    }

    static void updateTab(float deltaTime) {
        EbDesktopMenuShellState::tabVisual() =
            approach(EbDesktopMenuShellState::tabVisual(), 1.0f, EbDesktopMenuShellState::kTabTau,
                     deltaTime);
    }

    static void beginTabSwitch(SettingsCategory next) {
        SettingsCategory const prev = EbDesktopMenuShellState::active();
        if (next == prev || next == SettingsCategory::Count) {
            return;
        }
        int const from = static_cast<int>(prev);
        int const to = static_cast<int>(next);
        EbDesktopMenuShellState::tabDir() = to >= from ? 1 : -1;
        // Soft dip from wherever we are — no snap back to a full slide.
        float& tab = EbDesktopMenuShellState::tabVisual();
        if (tab > EbDesktopMenuShellState::kTabDip) {
            tab = EbDesktopMenuShellState::kTabDip;
        } else {
            tab = (std::max)(0.35f, tab * 0.85f);
        }
        EbDesktopMenuShellState::tabResetScroll() = true;
        EbDesktopMenuShellState::active() = next;
        EbDesktopMenuShellState::wheelDelta() = 0.0f;
    }

private:
    EbDesktopMenuShellAnim() = delete;
};

class EbDesktopMenuShellDraw {
public:
    static void navGroupLabel(char const* label) {
        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        EbDesktopMenuFonts::pushCaps();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.45f));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        EbDesktopMenuFonts::pop();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }

    static bool navTab(char const* label, bool selected, float width) {
        ImGui::PushID(label);
        EbDesktopMenuFonts::pushUi();
        float const rowH = ImGui::GetFontSize() + 12.0f;
        ImVec2 const pos = ImGui::GetCursorScreenPos();

        bool const pressed = ImGui::InvisibleButton("##tab", ImVec2(width, rowH));
        bool const hovered = ImGui::IsItemHovered();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (selected || hovered) {
            ImU32 const fill =
                selected ? EbDesktopMenuTheme::toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.14f))
                         : EbDesktopMenuTheme::toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.07f));
            drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + rowH), fill, 8.0f);
        }

        ImVec4 const textColor =
            selected ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 0.78f);
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                          ImVec2(pos.x + 12.0f, pos.y + (rowH - ImGui::GetFontSize()) * 0.5f),
                          EbDesktopMenuTheme::toU32(textColor), label);

        if (selected) {
            float const underlineY = pos.y + rowH - 3.0f;
            float const textW = ImGui::CalcTextSize(label).x;
            drawList->AddLine(ImVec2(pos.x + 12.0f, underlineY),
                              ImVec2(pos.x + 12.0f + textW, underlineY),
                              EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::accentLight()), 2.0f);
        }

        EbDesktopMenuFonts::pop();
        ImGui::PopID();
        return pressed;
    }

    static void drawSidebarNav(ImVec2 const& origin, float width) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 16.0f, origin.y + 20.0f));
        ImGui::BeginGroup();

        EbDesktopMenuFonts::pushHeader();
        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::text());
        ImGui::TextUnformatted("EARLYBUFFER");
        ImGui::PopStyleColor();
        EbDesktopMenuFonts::pop();

        std::string version = "v?.?.?";
        if (auto* mod = geode::prelude::Mod::get()) {
            version = mod->getVersion().toVString();
        }
        EbDesktopMenuFonts::pushCaps();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.42f));
        ImGui::TextUnformatted(version.c_str());
        ImGui::PopStyleColor();
        EbDesktopMenuFonts::pop();

        float const tabWidth = width - 32.0f;

        navGroupLabel("SETTINGS");
        SettingsCategory const settingsTabs[] = {
            SettingsCategory::Main,   SettingsCategory::Scope, SettingsCategory::Assist,
            SettingsCategory::Bake,   SettingsCategory::Modes, SettingsCategory::Visual,
            SettingsCategory::Keys};
        for (SettingsCategory category : settingsTabs) {
            bool const selected = EbDesktopMenuShellState::active() == category;
            if (navTab(EbInLevelSettingsFilter::categoryName(category), selected, tabWidth)) {
                EbDesktopMenuShellAnim::beginTabSwitch(category);
            }
        }

        navGroupLabel("DEVELOPER");
        {
            bool const selected = EbDesktopMenuShellState::active() == SettingsCategory::Dev;
            if (navTab(EbInLevelSettingsFilter::categoryName(SettingsCategory::Dev), selected,
                       tabWidth)) {
                EbDesktopMenuShellAnim::beginTabSwitch(SettingsCategory::Dev);
            }
        }

        ImGui::EndGroup();
    }

    static void drawContentBody(ImVec2 const& origin, float width, float height, float tabAlpha,
                                float tabSlideX) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + tabSlideX, origin.y));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * tabAlpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(EbDesktopMenuShellState::kContentPadX,
                                   EbDesktopMenuShellState::kContentPadY));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 8.0f));
        // Hide bar via size 0 — keep scroll extent (NoScrollbar can clip last rows).
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
        // AlwaysUseWindowPadding: borderless children ignore WindowPadding otherwise,
        // so the section card would fill the tab 1:1 with no bg gutter.
        ImGui::BeginChild("##eb-shell-tab", ImVec2(width, height),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);

        if (EbDesktopMenuShellState::tabResetScroll()) {
            ImGui::SetScrollY(0.0f);
            EbDesktopMenuShellState::tabResetScroll() = false;
        }

        SettingsCategory const active = EbDesktopMenuShellState::active();
        char const* name = EbInLevelSettingsFilter::categoryName(active);

        // Sidebar already shows the active tab — no duplicate category title here.
        char sectionId[64];
        std::snprintf(sectionId, sizeof(sectionId), "smoke-%s", name);
        if (EbSectionCard::begin(sectionId, "SECTION")) {
            // Checkbox smoke (image 2): live bind + disabled dim row.
            EbCheckbox::drawSetting("enabled", "Enabled");
            static bool s_smokeLocal = true;
            EbCheckbox::draw("smoke-local", "Checkbox", &s_smokeLocal);
            static bool s_smokeDisabled = false;
            EbCheckbox::draw("smoke-disabled", "Checkbox disabled", &s_smokeDisabled, false);

            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            // Button smoke — full-width row chrome only (+ real eb-action).
            EbButton::draw("smoke-row", "Row button");
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            EbButton::drawAction("cycle-assist-mode", "Mode");

            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            // Slider smoke — wave fill + white knob; int setting + float local + px.
            EbSlider::drawIntSetting("ticks-all", "Window (Default)", 0, 48);
            static float s_smokeFloat = 5.0f;
            EbSlider::drawFloat("smoke-float", "Slider 1", &s_smokeFloat, 0.0f, 10.0f, 1, "F");
            static int s_smokePx = 12;
            EbSlider::drawInt("smoke-px", "Slider px", &s_smokePx, 0, 64, "px");

            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            // AP11 combo smoke — short one-of enums.
            {
                char const* modeOpts[] = {"Live", "Replay", "Hybrid", "BruteForce"};
                EbComboBox::drawSetting("assist-mode", "Assist Mode", modeOpts, 4);
                char const* unitOpts[] = {"Ticks", "Frames"};
                EbComboBox::drawSetting("window-unit", "Window Unit", unitOpts, 2);
            }

            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            // AP12 dropdown smoke — full-width field + longer scrollable list.
            {
                char const* objectiveOpts[] = {"survive_then_endx_v1", "planner_mirror_v1",
                                               "horizon_clear_v1"};
                char const* objectiveLabels[] = {"Survive then EndX", "Planner Mirror",
                                                 "Horizon Clear"};
                EbDropdown::drawSetting("dev-objective", "Dev Objective", objectiveOpts, 3,
                                        objectiveLabels);
                static int s_smokeDrop = 2;
                char const* longOpts[] = {"Alpha",  "Bravo", "Charlie", "Delta", "Echo",
                                          "Foxtrot", "Golf",  "Hotel",   "India", "Juliet"};
                EbDropdown::draw("smoke-long-drop", "Long List", longOpts, 10, &s_smokeDrop);
            }

            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            // AP13 color smoke — swatch + classic hue-bar popup picker.
            EbColorPicker::drawSetting("save-icon-color", "Save Icon Color");
            static float s_smokeRgb[3] = {0.48f, 0.38f, 1.0f};
            EbColorPicker::draw("smoke-color", "Accent Smoke", s_smokeRgb);

            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            // AP14 keybind smoke — capture field + clear; hotkeys gated while capturing.
            EbKeybind::drawSetting("hotkey-desktop-menu", "Desktop Menu");
            EbKeybind::drawSetting("hotkey-debug", "Debug Logging");
        }
        EbSectionCard::end();
        // Extra scroll extent so the last rows clear the clip edge.
        ImGui::Dummy(ImVec2(0.0f, EbDesktopMenuShellState::kContentPadY + 24.0f));

        // Apply wheel after layout so ScrollMaxY includes this frame's content.
        float const wheel = EbDesktopMenuShell::takeWheelDelta();
        ImVec2 const mouse = ImGui::GetIO().MousePos;
        bool const overTab = mouse.x >= origin.x && mouse.x < origin.x + width &&
                             mouse.y >= origin.y && mouse.y < origin.y + height;
        if (wheel != 0.0f && overTab && EbDesktopMenuShellState::tabVisual() >= 0.97f) {
            float const step = ImGui::GetTextLineHeightWithSpacing();
            float next = ImGui::GetScrollY() + wheel * step;
            float const maxY = ImGui::GetScrollMaxY();
            if (next < 0.0f) {
                next = 0.0f;
            }
            if (next > maxY) {
                next = maxY;
            }
            ImGui::SetScrollY(next);
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(4);
    }

    static void frame() {
        float const deltaTime = (std::max)(0.0f, ImGui::GetIO().DeltaTime);
        EbDesktopMenuShellAnim::updateOpen(deltaTime);
        EbDesktopMenuShellAnim::updateTab(deltaTime);

        float const openE = EbDesktopMenuShellState::openVisual();
        if (openE <= 0.001f && !EbDesktopMenuShellState::wantOpen()) {
            return;
        }

        ImGuiIO const& io = ImGui::GetIO();
        ImVec2 const center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        float const slideY = (1.0f - openE) * EbDesktopMenuShellState::kOpenSlidePx;

        ImGui::SetNextWindowSize(
            ImVec2(EbDesktopMenuShellState::kMenuWidth, EbDesktopMenuShellState::kMenuHeight),
            ImGuiCond_Always);
        // Always anchored — avoids Appearing/Always flips that jumped on spam-toggle.
        ImGui::SetNextWindowPos(ImVec2(center.x, center.y + slideY), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, openE);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoMove;
        // Block input until mostly open / while closing.
        if (openE < 0.98f || !EbDesktopMenuShellState::wantOpen()) {
            flags |= ImGuiWindowFlags_NoInputs;
        }
        if (!ImGui::Begin("##eb-desktop-shell", nullptr, flags)) {
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(4);
            return;
        }

        ImVec2 const winPos = ImGui::GetWindowPos();
        ImVec2 const winSize = ImGui::GetWindowSize();
        float const rounding = EbDesktopMenuTheme::windowRounding();
        float const sideW = EbDesktopMenuShellState::kSidebarWidth;
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Unified shell — sidebar docked flush left (outer corners only).
        drawList->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y),
                                EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::bg()), rounding);

        ImVec2 const sideMin = winPos;
        ImVec2 const sideMax(winPos.x + sideW, winPos.y + winSize.y);
        EbDesktopMenuSidebarSmoke::get().paint(drawList, sideMin, sideMax, rounding,
                                               static_cast<float>(ImGui::GetTime()),
                                               ImDrawFlags_RoundCornersLeft);

        drawSidebarNav(sideMin, sideW);

        float const tabE = EbDesktopMenuShellState::tabVisual();
        float const tabSlide =
            (1.0f - tabE) * static_cast<float>(EbDesktopMenuShellState::tabDir()) *
            EbDesktopMenuShellState::kTabSlidePx;
        // Soft fade — stays readable on interrupt (dip floors around kTabDip).
        float const tabAlpha = 0.55f + 0.45f * tabE;
        drawContentBody(ImVec2(winPos.x + sideW, winPos.y), winSize.x - sideW, winSize.y, tabAlpha,
                        tabSlide);

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(4);
    }

private:
    EbDesktopMenuShellDraw() = delete;
};

SettingsCategory EbDesktopMenuShell::activeCategory() {
    return EbDesktopMenuShellState::active();
}

void EbDesktopMenuShell::setActiveCategory(SettingsCategory category) {
    EbDesktopMenuShellAnim::beginTabSwitch(category);
}

void EbDesktopMenuShell::requestOpen(bool open) {
    EbDesktopMenuShellState::wantOpen() = open;
}

bool EbDesktopMenuShell::wantsOpen() {
    return EbDesktopMenuShellState::wantOpen();
}

bool EbDesktopMenuShell::shouldKeepVisible() {
    return EbDesktopMenuShellState::wantOpen() || EbDesktopMenuShellState::openVisual() > 0.001f;
}

void EbDesktopMenuShell::addWheelDelta(float deltaY) {
    EbDesktopMenuShellState::wheelDelta() += deltaY;
}

float EbDesktopMenuShell::takeWheelDelta() {
    float const delta = EbDesktopMenuShellState::wheelDelta();
    EbDesktopMenuShellState::wheelDelta() = 0.0f;
    return delta;
}

void EbDesktopMenuShell::draw() {
    EbDesktopMenuShellDraw::frame();
}

#else

SettingsCategory EbDesktopMenuShell::activeCategory() {
    return SettingsCategory::Main;
}

void EbDesktopMenuShell::setActiveCategory(SettingsCategory category) {
    (void)category;
}

void EbDesktopMenuShell::requestOpen(bool) {}

bool EbDesktopMenuShell::wantsOpen() {
    return false;
}

bool EbDesktopMenuShell::shouldKeepVisible() {
    return false;
}

void EbDesktopMenuShell::addWheelDelta(float) {}

float EbDesktopMenuShell::takeWheelDelta() {
    return 0.0f;
}

void EbDesktopMenuShell::draw() {}

#endif
