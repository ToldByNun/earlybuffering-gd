#include "eb_dropdown.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "../desktop_menu_bind.hpp"
#include "../desktop_menu_fonts.hpp"
#include "../desktop_menu_motion.hpp"
#include "../desktop_menu_theme.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <string>

class EbDropdownDraw {
public:
    static constexpr float kFieldPadX = 14.0f;
    static constexpr float kFieldPadY = 10.0f;
    static constexpr float kCaretW = 9.0f;
    static constexpr float kLabelGap = 6.0f;
    static constexpr int kVisibleOptions = 7;

    static int findIndex(char const* const* options, int optionCount, char const* value) {
        if (!options || optionCount <= 0 || !value) {
            return 0;
        }
        for (int index = 0; index < optionCount; ++index) {
            if (options[index] && std::strcmp(options[index], value) == 0) {
                return index;
            }
        }
        return 0;
    }

    static char const* displayAt(char const* const* options, char const* const* displayLabels,
                                 int index) {
        if (displayLabels && displayLabels[index] && displayLabels[index][0] != '\0') {
            return displayLabels[index];
        }
        if (options && options[index]) {
            return options[index];
        }
        return "";
    }

    static void drawCaret(ImDrawList* drawList, ImVec2 const& tip, float size, ImU32 color) {
        if (!drawList) {
            return;
        }
        drawList->AddTriangleFilled(ImVec2(tip.x, tip.y), ImVec2(tip.x + size, tip.y),
                                    ImVec2(tip.x + size * 0.5f, tip.y + size * 0.65f), color);
    }

    static ImVec4 idleFill() {
        return EbDesktopMenuTheme::surface();
    }

    static ImVec4 hoverFill() {
        return ImVec4(0.18f, 0.18f, 0.24f, 1.0f);
    }

    static ImVec4 selectedFill() {
        return ImVec4(0.24f, 0.24f, 0.32f, 1.0f);
    }

    static bool drawBlock(char const* id, char const* label, char const* const* options,
                          int optionCount, int* selectedIndex, char const* const* displayLabels,
                          bool enabled) {
        if (!id || id[0] == '\0' || !label || !options || optionCount <= 0 || !selectedIndex) {
            return false;
        }
        if (*selectedIndex < 0 || *selectedIndex >= optionCount) {
            *selectedIndex = 0;
        }
        char const* current =
            displayAt(options, displayLabels, *selectedIndex);

        ImGui::PushID(id);
        EbDesktopMenuFonts::pushUi();

        float const avail = ImGui::GetContentRegionAvail().x;
        float const textH = ImGui::GetFontSize();
        float const fieldH = textH + kFieldPadY * 2.0f;
        float const labelH = textH;
        float const blockH = labelH + kLabelGap + fieldH;
        float const rounding = EbDesktopMenuTheme::frameRounding() - 2.0f;

        ImVec2 const blockMin = ImGui::GetCursorScreenPos();
        bool openPopup = false;
        if (enabled) {
            openPopup = ImGui::InvisibleButton("##eb-drop-block", ImVec2(avail, blockH));
        } else {
            ImGui::Dummy(ImVec2(avail, blockH));
        }
        bool const hovered = enabled && ImGui::IsItemHovered();
        if (openPopup) {
            ImGui::OpenPopup("##eb-drop-popup");
        }
        bool const open = ImGui::IsPopupOpen("##eb-drop-popup");
        float const heat = enabled ? EbDesktopMenuMotion::smoothToward(ImGui::GetItemID(),
                                                                       hovered || open, 0.05f)
                                   : 0.0f;

        ImVec2 const fieldMin(blockMin.x, blockMin.y + labelH + kLabelGap);
        ImVec2 const fieldMax(blockMin.x + avail, fieldMin.y + fieldH);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec4 labelColor = EbDesktopMenuTheme::muted();
        if (!enabled) {
            labelColor = ImVec4(0.62f, 0.64f, 0.70f, 0.92f);
        } else {
            labelColor = EbDesktopMenuMotion::lerp4(labelColor, EbDesktopMenuTheme::text(), heat);
        }
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), blockMin,
                          EbDesktopMenuTheme::toU32(labelColor), label);

        ImVec4 fill = idleFill();
        if (!enabled) {
            fill.w = 0.75f;
        } else {
            fill = EbDesktopMenuMotion::lerp4(fill, hoverFill(), heat);
        }
        drawList->AddRectFilled(fieldMin, fieldMax, EbDesktopMenuTheme::toU32(fill), rounding);

        ImVec4 stroke = EbDesktopMenuTheme::outline();
        stroke.w = enabled ? 0.7f : 0.4f;
        if (enabled) {
            stroke = EbDesktopMenuMotion::lerp4(stroke, ImVec4(0.55f, 0.57f, 0.66f, 0.95f), heat);
        }
        drawList->AddRect(fieldMin, fieldMax, EbDesktopMenuTheme::toU32(stroke), rounding, 0,
                          EbDesktopMenuTheme::outlineThickness());

        ImVec4 valueColor = EbDesktopMenuTheme::muted();
        if (!enabled) {
            valueColor = ImVec4(0.62f, 0.64f, 0.70f, 0.85f);
        } else {
            valueColor = EbDesktopMenuMotion::lerp4(valueColor, EbDesktopMenuTheme::text(), heat);
        }
        ImVec2 const valueSize = ImGui::CalcTextSize(current);
        ImVec2 const valuePos(fieldMin.x + kFieldPadX,
                              fieldMin.y + (fieldH - valueSize.y) * 0.5f);
        // Clip long labels before the caret.
        drawList->PushClipRect(
            fieldMin, ImVec2(fieldMax.x - kFieldPadX - kCaretW - 6.0f, fieldMax.y), true);
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), valuePos,
                          EbDesktopMenuTheme::toU32(valueColor), current);
        drawList->PopClipRect();
        drawCaret(drawList,
                  ImVec2(fieldMax.x - kFieldPadX - kCaretW,
                         fieldMin.y + (fieldH - kCaretW * 0.65f) * 0.5f),
                  kCaretW, EbDesktopMenuTheme::toU32(valueColor));

        bool changed = false;
        // Open anim only — deferred close kept a modal popup and blocked other widgets.
        float const openAmt =
            EbDesktopMenuMotion::smoothToward(ImGui::GetID("##eb-drop-open"), open, 0.055f);
        float const optH = fieldH - 2.0f;
        float const listPad = 5.0f;
        int const visible = (std::min)(optionCount, kVisibleOptions);
        float const fullH = listPad * 2.0f + visible * optH + (visible - 1) * 3.0f;
        float const animH = (std::max)(listPad * 2.0f + optH, fullH * (0.18f + 0.82f * openAmt));
        ImGui::SetNextWindowSize(ImVec2(avail, open ? animH : fullH), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(fieldMin.x, fieldMax.y + 6.0f), ImGuiCond_Appearing);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.35f + 0.65f * openAmt);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(listPad, listPad));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, EbDesktopMenuTheme::panel());
        ImGui::PushStyleColor(ImGuiCol_Border, EbDesktopMenuTheme::outline());
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.35f, 0.35f, 0.42f, 0.9f));
        if (ImGui::BeginPopup("##eb-drop-popup",
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
            float const popupInner = ImGui::GetContentRegionAvail().x;
            for (int index = 0; index < optionCount; ++index) {
                char const* option = displayAt(options, displayLabels, index);
                ImGui::PushID(index);
                bool const selected = index == *selectedIndex;
                ImVec2 const optMin = ImGui::GetCursorScreenPos();
                bool const clicked = ImGui::InvisibleButton("##opt", ImVec2(popupInner, optH));
                bool const optHover = ImGui::IsItemHovered();
                float const optHeat =
                    EbDesktopMenuMotion::smoothToward(ImGui::GetItemID(), optHover || selected, 0.05f);
                ImDrawList* popupDraw = ImGui::GetWindowDrawList();
                ImVec2 const optMax(optMin.x + popupInner, optMin.y + optH);

                ImVec4 optFill = selected ? selectedFill() : hoverFill();
                if (optHeat > 0.01f) {
                    popupDraw->AddRectFilled(optMin, optMax,
                                             EbDesktopMenuTheme::toU32(ImVec4(
                                                 optFill.x, optFill.y, optFill.z, optFill.w * optHeat)),
                                             rounding - 2.0f);
                }

                ImVec4 optText = EbDesktopMenuMotion::lerp4(EbDesktopMenuTheme::muted(),
                                                            EbDesktopMenuTheme::text(), optHeat);
                popupDraw->AddText(
                    ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(optMin.x + 12.0f, optMin.y + (optH - textH) * 0.5f),
                    EbDesktopMenuTheme::toU32(optText), option);

                if (clicked) {
                    if (index != *selectedIndex) {
                        *selectedIndex = index;
                        changed = true;
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(6);

        EbDesktopMenuFonts::pop();
        ImGui::PopID();
        return changed;
    }

private:
    EbDropdownDraw() = delete;
};

bool EbDropdown::draw(char const* id, char const* label, char const* const* options,
                      int optionCount, int* selectedIndex, char const* const* displayLabels,
                      bool enabled) {
    return EbDropdownDraw::drawBlock(id, label, options, optionCount, selectedIndex, displayLabels,
                                     enabled);
}

bool EbDropdown::drawSetting(char const* key, char const* label, char const* const* options,
                             int optionCount, char const* const* displayLabels, bool enabled) {
    if (!key || key[0] == '\0' || !options || optionCount <= 0) {
        return false;
    }
    if (!EbDesktopMenuBind::exists(key)) {
        return false;
    }
    std::string const current = EbDesktopMenuBind::getString(key, options[0] ? options[0] : "");
    int selected = EbDropdownDraw::findIndex(options, optionCount, current.c_str());
    if (!EbDropdownDraw::drawBlock(key, label, options, optionCount, &selected, displayLabels,
                                   enabled)) {
        return false;
    }
    if (!options[selected]) {
        return false;
    }
    return EbDesktopMenuBind::setString(key, options[selected]);
}

#else

bool EbDropdown::draw(char const* id, char const* label, char const* const* options,
                      int optionCount, int* selectedIndex, char const* const* displayLabels,
                      bool enabled) {
    (void)id;
    (void)label;
    (void)options;
    (void)optionCount;
    (void)selectedIndex;
    (void)displayLabels;
    (void)enabled;
    return false;
}

bool EbDropdown::drawSetting(char const* key, char const* label, char const* const* options,
                             int optionCount, char const* const* displayLabels, bool enabled) {
    (void)key;
    (void)label;
    (void)options;
    (void)optionCount;
    (void)displayLabels;
    (void)enabled;
    return false;
}

#endif
