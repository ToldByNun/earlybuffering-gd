#include "eb_section_card.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "../desktop_menu_fonts.hpp"
#include "../desktop_menu_theme.hpp"
#include <imgui.h>

class EbSectionCardState {
public:
    static int& depth() {
        static int value = 0;
        return value;
    }

private:
    EbSectionCardState() = delete;
};

bool EbSectionCard::begin(char const* id, char const* title) {
    if (!id || id[0] == '\0' || !title) {
        return false;
    }

    ImGui::PushID(id);

    float const width = ImGui::GetContentRegionAvail().x;
    float const rounding = EbDesktopMenuTheme::frameRounding();
    ImVec4 border = EbDesktopMenuTheme::outline();
    border.w = 0.55f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, EbDesktopMenuTheme::panel());
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, EbDesktopMenuTheme::outlineThickness());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));

    ImGuiChildFlags const childFlags = ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders;
    ImGui::BeginChild("##eb-section-card", ImVec2(width, 0.0f), childFlags,
                      ImGuiWindowFlags_NoScrollbar);

    EbDesktopMenuFonts::pushCaps();
    ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::muted());
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    EbDesktopMenuFonts::pop();
    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    EbSectionCardState::depth() += 1;
    return true;
}

void EbSectionCard::end() {
    if (EbSectionCardState::depth() <= 0) {
        return;
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
    ImGui::PopID();

    ImGui::Dummy(ImVec2(0.0f, 12.0f));
    EbSectionCardState::depth() -= 1;
}

#else

bool EbSectionCard::begin(char const* id, char const* title) {
    (void)id;
    (void)title;
    return false;
}

void EbSectionCard::end() {}

#endif
