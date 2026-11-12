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

    // Inner pad so widgets (checkbox, slider value, …) clear the card edge.
    static constexpr float kPadX = 18.0f;
    static constexpr float kPadY = 16.0f;

private:
    EbSectionCardState() = delete;
};

bool EbSectionCard::begin(char const* id, char const* title) {
    if (!id || id[0] == '\0' || !title) {
        return false;
    }

    // Gap between cards only — not after the last (that doubled bottom scroll pad).
    float const padY = ImGui::GetStyle().WindowPadding.y;
    if (ImGui::GetCursorPosY() > padY + 1.0f) {
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
    }

    ImGui::PushID(id);

    ImVec4 border = EbDesktopMenuTheme::outline();
    border.w = 0.55f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, EbDesktopMenuTheme::panel());
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, EbDesktopMenuTheme::frameRounding());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, EbDesktopMenuTheme::outlineThickness());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(EbSectionCardState::kPadX, EbSectionCardState::kPadY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));

    // Auto-height card inside the scrolling tab. Inner WindowPadding insets
    // widgets on all sides so nothing sits flush / clipped at the card edge.
    // NoScrollWithMouse: tab host owns wheel (Cocos → SetScrollY).
    float const cardWidth = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("##eb-section-body", ImVec2(cardWidth, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

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

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::EndChild();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
    ImGui::PopID();
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
