#include "eb_button.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "../desktop_menu_fonts.hpp"
#include "../desktop_menu_sidebar_smoke.hpp"
#include "../desktop_menu_theme.hpp"
#include "../../settings/action_button_setting.hpp"
#include <imgui.h>
#include <algorithm>
#include <string>

class EbButtonDraw {
public:
    static bool drawChrome(char const* id, char const* label, bool enabled, EbButton::Style style) {
        if (!id || id[0] == '\0' || !label) {
            return false;
        }

        ImGui::PushID(id);
        EbDesktopMenuFonts::pushUi();

        ImVec2 const textSize = ImGui::CalcTextSize(label);
        float const padX = style == EbButton::Style::Primary ? 18.0f : 14.0f;
        float const padY = style == EbButton::Style::Primary ? 10.0f : 9.0f;
        float const height = textSize.y + padY * 2.0f;
        float const avail = ImGui::GetContentRegionAvail().x;
        float width = style == EbButton::Style::Row ? avail : (textSize.x + padX * 2.0f);
        width = (std::min)(width, avail);
        // Real pill radius = half height (avoid theme pillRounding 999 blowing remask/glow).
        float const rounding = style == EbButton::Style::Primary
                                   ? height * 0.5f
                                   : EbDesktopMenuTheme::frameRounding();

        ImVec2 const min = ImGui::GetCursorScreenPos();
        if (style == EbButton::Style::Primary) {
            // Keep pills left-aligned in section rows (not stretched).
        }
        ImVec2 const max(min.x + width, min.y + height);

        bool pressed = false;
        if (enabled) {
            pressed = ImGui::InvisibleButton("##eb-btn", ImVec2(width, height));
        } else {
            ImGui::Dummy(ImVec2(width, height));
        }
        bool const hovered = enabled && ImGui::IsItemHovered();
        bool const held = enabled && ImGui::IsItemActive();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 const outside = EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::panel());
        float const time = static_cast<float>(ImGui::GetTime());

        if (!enabled) {
            ImVec4 fill = EbDesktopMenuTheme::surface();
            fill.w = 0.85f;
            drawList->AddRectFilled(min, max, EbDesktopMenuTheme::toU32(fill), rounding);
            ImVec4 stroke = ImVec4(0.55f, 0.57f, 0.64f, 0.55f);
            drawList->AddRect(min, max, EbDesktopMenuTheme::toU32(stroke), rounding, 0,
                              EbDesktopMenuTheme::outlineThickness());
        } else if (style == EbButton::Style::Primary) {
            EbDesktopMenuSidebarSmoke::get().paint(drawList, min, max, rounding, time,
                                                   ImDrawFlags_RoundCornersAll, outside);
            if (hovered || held) {
                EbDesktopMenuTheme::drawAccentGlow(drawList, min, max, rounding,
                                                   held ? 0.85f : 0.55f);
            }
        } else {
            ImU32 const fill = EbDesktopMenuTheme::toU32(EbDesktopMenuTheme::surface());
            drawList->AddRectFilled(min, max, fill, rounding);
            ImVec4 stroke = EbDesktopMenuTheme::outline();
            if (hovered || held) {
                stroke = EbDesktopMenuTheme::accentLight();
                stroke.w = held ? 0.95f : 0.75f;
                // Soft wave tint on hover for row buttons.
                ImU32 const wave = EbDesktopMenuSidebarSmoke::sampleColor(time, 0.35f, 0.5f);
                drawList->AddRectFilled(min, max, (wave & 0x00FFFFFF) | 0x33000000, rounding);
            }
            drawList->AddRect(min, max, EbDesktopMenuTheme::toU32(stroke), rounding, 0,
                              EbDesktopMenuTheme::outlineThickness());
        }

        ImVec4 textColor = EbDesktopMenuTheme::text();
        if (!enabled) {
            textColor = ImVec4(0.62f, 0.64f, 0.70f, 0.92f);
        }
        ImVec2 const textPos(min.x + (width - textSize.x) * 0.5f,
                             min.y + (height - textSize.y) * 0.5f);
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), textPos,
                          EbDesktopMenuTheme::toU32(textColor), label);

        EbDesktopMenuFonts::pop();
        ImGui::PopID();
        return pressed;
    }

private:
    EbButtonDraw() = delete;
};

bool EbButton::draw(char const* id, char const* label, bool enabled, Style style) {
    return EbButtonDraw::drawChrome(id, label, enabled, style);
}

bool EbButton::drawAction(char const* actionKey, char const* fallbackLabel, Style style) {
    if (!actionKey || actionKey[0] == '\0') {
        return false;
    }
    std::string const fallback = fallbackLabel ? fallbackLabel : "Go";
    std::string const label =
        EbActionButtonSettingImplementation::dynamicButtonLabel(actionKey, fallback);
    bool const enabled = EbActionButtonSettingImplementation::actionShouldEnable(actionKey);
    if (!EbButtonDraw::drawChrome(actionKey, label.c_str(), enabled, style)) {
        return false;
    }
    EbActionButtonSettingImplementation::fireSettingAction(actionKey);
    return true;
}

#else

bool EbButton::draw(char const* id, char const* label, bool enabled, Style style) {
    (void)id;
    (void)label;
    (void)enabled;
    (void)style;
    return false;
}

bool EbButton::drawAction(char const* actionKey, char const* fallbackLabel, Style style) {
    (void)actionKey;
    (void)fallbackLabel;
    (void)style;
    return false;
}

#endif
