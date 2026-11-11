#include "desktop_menu_shell.hpp"
#include "desktop_menu_fonts.hpp"
#include "desktop_menu_sidebar_smoke.hpp"
#include "desktop_menu_theme.hpp"
#include "widgets/eb_button.hpp"
#include "widgets/eb_checkbox.hpp"
#include "widgets/eb_color_picker.hpp"
#include "widgets/eb_combo_box.hpp"
#include "widgets/eb_dropdown.hpp"
#include "widgets/eb_section_card.hpp"
#include "widgets/eb_slider.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include <Geode/Geode.hpp>
#include <imgui.h>
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

    static constexpr float kSidebarWidth = 196.0f;
    static constexpr float kMenuWidth = 900.0f;
    static constexpr float kMenuHeight = 560.0f;
    // Inset so section cards sit inside the tab — bg shows around the card.
    static constexpr float kContentPadX = 28.0f;
    static constexpr float kContentPadY = 24.0f;

private:
    EbDesktopMenuShellState() = delete;
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
                EbDesktopMenuShellState::active() = category;
            }
        }

        navGroupLabel("DEVELOPER");
        {
            bool const selected = EbDesktopMenuShellState::active() == SettingsCategory::Dev;
            if (navTab(EbInLevelSettingsFilter::categoryName(SettingsCategory::Dev), selected,
                       tabWidth)) {
                EbDesktopMenuShellState::active() = SettingsCategory::Dev;
            }
        }

        ImGui::EndGroup();
    }

    static void drawContentBody(ImVec2 const& origin, float width, float height) {
        ImGui::SetCursorScreenPos(origin);
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
        }
        EbSectionCard::end();
        // Extra scroll extent so the last rows clear the clip edge.
        ImGui::Dummy(ImVec2(0.0f, EbDesktopMenuShellState::kContentPadY + 24.0f));

        // Apply wheel after layout so ScrollMaxY includes this frame's content.
        float const wheel = EbDesktopMenuShell::takeWheelDelta();
        ImVec2 const mouse = ImGui::GetIO().MousePos;
        bool const overTab = mouse.x >= origin.x && mouse.x < origin.x + width &&
                             mouse.y >= origin.y && mouse.y < origin.y + height;
        if (wheel != 0.0f && overTab) {
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
        ImGui::PopStyleVar(3);
    }

    static void frame() {
        ImGuiIO const& io = ImGui::GetIO();
        ImGui::SetNextWindowSize(
            ImVec2(EbDesktopMenuShellState::kMenuWidth, EbDesktopMenuShellState::kMenuHeight),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                                ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
        if (!ImGui::Begin("##eb-desktop-shell", nullptr, flags)) {
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
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
        drawContentBody(ImVec2(winPos.x + sideW, winPos.y), winSize.x - sideW, winSize.y);

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

private:
    EbDesktopMenuShellDraw() = delete;
};

SettingsCategory EbDesktopMenuShell::activeCategory() {
    return EbDesktopMenuShellState::active();
}

void EbDesktopMenuShell::setActiveCategory(SettingsCategory category) {
    if (category == SettingsCategory::Count) {
        return;
    }
    EbDesktopMenuShellState::active() = category;
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

void EbDesktopMenuShell::addWheelDelta(float) {}

float EbDesktopMenuShell::takeWheelDelta() {
    return 0.0f;
}

void EbDesktopMenuShell::draw() {}

#endif
