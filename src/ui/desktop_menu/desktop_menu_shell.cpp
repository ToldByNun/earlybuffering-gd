#include "desktop_menu_shell.hpp"
#include "desktop_menu_theme.hpp"

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

    static constexpr float kSidebarWidth = 200.0f;
    static constexpr float kMenuWidth = 900.0f;
    static constexpr float kMenuHeight = 560.0f;

private:
    EbDesktopMenuShellState() = delete;
};

class EbDesktopMenuShellDraw {
public:
    static void navGroupLabel(char const* label) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.50f));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    static bool navTab(char const* label, bool selected, float width) {
        ImGui::PushID(label);
        float const rowH = ImGui::GetFontSize() + 14.0f;
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
        drawList->AddText(ImVec2(pos.x + 12.0f, pos.y + (rowH - ImGui::GetFontSize()) * 0.5f),
                          EbDesktopMenuTheme::toU32(textColor), label);

        if (selected) {
            float const underlineY = pos.y + rowH - 4.0f;
            float const textW = ImGui::CalcTextSize(label).x;
            drawList->AddLine(ImVec2(pos.x + 12.0f, underlineY),
                              ImVec2(pos.x + 12.0f + textW, underlineY),
                              EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::accentLight()), 2.0f);
        }

        ImGui::PopID();
        return pressed;
    }

    static void drawSidebarNav(ImVec2 const& origin, float width) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 14.0f, origin.y + 18.0f));
        ImGui::BeginGroup();

        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::text());
        ImGui::TextUnformatted("EARLYBUFFER");
        ImGui::PopStyleColor();

        std::string version = "v?.?.?";
        if (auto* mod = geode::prelude::Mod::get()) {
            version = mod->getVersion().toVString();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.45f));
        ImGui::TextUnformatted(version.c_str());
        ImGui::PopStyleColor();

        float const tabWidth = width - 28.0f;

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
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 18.0f));
        ImGui::BeginChild("##eb-shell-section", ImVec2(width, height), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground |
                              ImGuiWindowFlags_NoScrollWithMouse);

        SettingsCategory const active = EbDesktopMenuShellState::active();
        char const* name = EbInLevelSettingsFilter::categoryName(active);
        char header[64];
        std::snprintf(header, sizeof(header), "%s", name);
        for (char* cursor = header; *cursor; ++cursor) {
            if (*cursor >= 'a' && *cursor <= 'z') {
                *cursor = static_cast<char>(*cursor - 'a' + 'A');
            }
        }

        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::text());
        ImGui::TextUnformatted(header);
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Separator, EbDesktopMenuTheme::outline());
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::muted());
        ImGui::TextWrapped("Content host for category \"%s\". Widgets bind in later APs.", name);
        ImGui::PopStyleColor();

        ImGui::EndChild();
        ImGui::PopStyleVar();
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

        // Use window rect — not cursor/avail (those shift after nav widgets).
        ImVec2 const winPos = ImGui::GetWindowPos();
        ImVec2 const winSize = ImGui::GetWindowSize();
        float const rounding = EbDesktopMenuTheme::windowRounding();
        float const sideW = EbDesktopMenuShellState::kSidebarWidth;
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Single shell fill (all corners). Content is this same surface — no second panel.
        drawList->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y),
                                EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::bg()), rounding);

        // Sidebar paint on top of the left strip ( +1px into content kills AA seam ).
        drawList->AddRectFilled(winPos, ImVec2(winPos.x + sideW + 1.0f, winPos.y + winSize.y),
                                EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::sidebarBg()),
                                rounding, ImDrawFlags_RoundCornersLeft);

        drawSidebarNav(winPos, sideW);
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

void EbDesktopMenuShell::draw() {}

#endif
