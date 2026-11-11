#include "eb_button.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "../desktop_menu_fonts.hpp"
#include "../desktop_menu_theme.hpp"
#include "../../settings/action_button_setting.hpp"
#include <imgui.h>
#include <string>

class EbButtonDraw {
public:
    static bool drawChrome(char const* id, char const* label, bool enabled) {
        if (!id || id[0] == '\0' || !label) {
            return false;
        }

        ImGui::PushID(id);
        EbDesktopMenuFonts::pushUi();

        ImVec2 const textSize = ImGui::CalcTextSize(label);
        float const padY = 9.0f;
        float const height = textSize.y + padY * 2.0f;
        float const width = ImGui::GetContentRegionAvail().x;
        float const rounding = EbDesktopMenuTheme::frameRounding();

        ImVec2 const min = ImGui::GetCursorScreenPos();
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

        ImVec4 fill = EbDesktopMenuTheme::surface();
        if (!enabled) {
            fill.w = 0.85f;
        } else if (held) {
            fill = ImVec4(0.22f, 0.22f, 0.30f, 1.0f);
        } else if (hovered) {
            fill = ImVec4(0.18f, 0.18f, 0.24f, 1.0f);
        }
        drawList->AddRectFilled(min, max, EbDesktopMenuTheme::toU32(fill), rounding);

        ImVec4 stroke = EbDesktopMenuTheme::outline();
        if (!enabled) {
            stroke = ImVec4(0.55f, 0.57f, 0.64f, 0.55f);
        } else if (hovered || held) {
            stroke = EbDesktopMenuTheme::accentLight();
            stroke.w = held ? 0.95f : 0.75f;
        }
        drawList->AddRect(min, max, EbDesktopMenuTheme::toU32(stroke), rounding, 0,
                          EbDesktopMenuTheme::outlineThickness());

        ImVec4 textColor = EbDesktopMenuTheme::text();
        if (!enabled) {
            textColor = ImVec4(0.62f, 0.64f, 0.70f, 0.92f);
        } else if (!hovered && !held) {
            textColor = EbDesktopMenuTheme::muted();
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

bool EbButton::draw(char const* id, char const* label, bool enabled) {
    return EbButtonDraw::drawChrome(id, label, enabled);
}

bool EbButton::drawAction(char const* actionKey, char const* fallbackLabel) {
    if (!actionKey || actionKey[0] == '\0') {
        return false;
    }
    std::string const fallback = fallbackLabel ? fallbackLabel : "Go";
    std::string const label =
        EbActionButtonSettingImplementation::dynamicButtonLabel(actionKey, fallback);
    bool const enabled = EbActionButtonSettingImplementation::actionShouldEnable(actionKey);
    if (!EbButtonDraw::drawChrome(actionKey, label.c_str(), enabled)) {
        return false;
    }
    EbActionButtonSettingImplementation::fireSettingAction(actionKey);
    return true;
}

#else

bool EbButton::draw(char const* id, char const* label, bool enabled) {
    (void)id;
    (void)label;
    (void)enabled;
    return false;
}

bool EbButton::drawAction(char const* actionKey, char const* fallbackLabel) {
    (void)actionKey;
    (void)fallbackLabel;
    return false;
}

#endif
