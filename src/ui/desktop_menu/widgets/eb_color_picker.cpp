#include "eb_color_picker.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "../desktop_menu_bind.hpp"
#include "../desktop_menu_fonts.hpp"
#include "../desktop_menu_motion.hpp"
#include "../desktop_menu_theme.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

class EbColorPickerDraw {
public:
    static constexpr float kSwatchW = 52.0f;
    static constexpr float kSwatchPadY = 6.0f;

    static void clampRgb(float rgb[3]) {
        if (!rgb) {
            return;
        }
        for (int index = 0; index < 3; ++index) {
            if (rgb[index] < 0.0f) {
                rgb[index] = 0.0f;
            }
            if (rgb[index] > 1.0f) {
                rgb[index] = 1.0f;
            }
        }
    }

    static bool nearlyEqual(float const left[3], float const right[3]) {
        for (int index = 0; index < 3; ++index) {
            if (std::fabs(left[index] - right[index]) > 0.0005f) {
                return false;
            }
        }
        return true;
    }

    static void formatHex(char* out, size_t outSize, float const rgb[3]) {
        if (!out || outSize == 0 || !rgb) {
            return;
        }
        int const r = static_cast<int>(rgb[0] * 255.0f + 0.5f);
        int const g = static_cast<int>(rgb[1] * 255.0f + 0.5f);
        int const b = static_cast<int>(rgb[2] * 255.0f + 0.5f);
        std::snprintf(out, outSize, "#%02X%02X%02X", r, g, b);
    }

    static bool drawRow(char const* id, char const* label, float rgb[3], bool enabled) {
        if (!id || id[0] == '\0' || !label || !rgb) {
            return false;
        }
        clampRgb(rgb);

        ImGui::PushID(id);
        EbDesktopMenuFonts::pushUi();

        float const avail = ImGui::GetContentRegionAvail().x;
        float const textH = ImGui::GetFontSize();
        float const swatchH = textH + kSwatchPadY * 2.0f;
        float const rowH = (std::max)(swatchH, textH + 10.0f);
        float const rounding = 8.0f;

        ImVec2 const rowMin = ImGui::GetCursorScreenPos();
        bool openPopup = false;
        if (enabled) {
            openPopup = ImGui::InvisibleButton("##eb-color-row", ImVec2(avail, rowH));
        } else {
            ImGui::Dummy(ImVec2(avail, rowH));
        }
        bool const hovered = enabled && ImGui::IsItemHovered();
        bool const open = ImGui::IsPopupOpen("##eb-color-popup");
        float const heat = enabled ? EbDesktopMenuMotion::smoothToward(ImGui::GetItemID(),
                                                                       hovered || open, 0.05f)
                                   : 0.0f;
        if (openPopup) {
            ImGui::OpenPopup("##eb-color-popup");
        }

        ImVec2 const swatchMin(rowMin.x + avail - kSwatchW, rowMin.y + (rowH - swatchH) * 0.5f);
        ImVec2 const swatchMax(swatchMin.x + kSwatchW, swatchMin.y + swatchH);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec4 labelColor = EbDesktopMenuTheme::muted();
        if (!enabled) {
            labelColor = ImVec4(0.62f, 0.64f, 0.70f, 0.92f);
        } else {
            labelColor = EbDesktopMenuMotion::lerp4(labelColor, EbDesktopMenuTheme::text(), heat);
        }
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                          ImVec2(rowMin.x, rowMin.y + (rowH - textH) * 0.5f),
                          EbDesktopMenuTheme::toU32(labelColor), label);

        ImU32 const fill = ImGui::ColorConvertFloat4ToU32(ImVec4(rgb[0], rgb[1], rgb[2], 1.0f));
        drawList->AddRectFilled(swatchMin, swatchMax, fill, rounding);

        ImVec4 stroke = EbDesktopMenuTheme::outline();
        stroke.w = enabled ? 0.75f : 0.4f;
        if (enabled) {
            stroke = EbDesktopMenuMotion::lerp4(stroke, ImVec4(0.55f, 0.57f, 0.66f, 0.95f), heat);
        }
        drawList->AddRect(swatchMin, swatchMax, EbDesktopMenuTheme::toU32(stroke), rounding, 0,
                          EbDesktopMenuTheme::outlineThickness());

        char hex[16];
        formatHex(hex, sizeof(hex), rgb);
        ImVec2 const hexSize = ImGui::CalcTextSize(hex);
        float const hexX = swatchMin.x - 10.0f - hexSize.x;
        if (hexX > rowMin.x + ImGui::CalcTextSize(label).x + 12.0f) {
            ImVec4 hexColor = EbDesktopMenuTheme::muted();
            if (enabled) {
                hexColor = EbDesktopMenuMotion::lerp4(hexColor, EbDesktopMenuTheme::text(), heat);
            }
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                              ImVec2(hexX, rowMin.y + (rowH - hexSize.y) * 0.5f),
                              EbDesktopMenuTheme::toU32(hexColor), hex);
        }

        bool changed = false;
        float draft[3] = {rgb[0], rgb[1], rgb[2]};

        ImGui::SetNextWindowPos(ImVec2(swatchMin.x + kSwatchW - 260.0f, swatchMax.y + 6.0f),
                                ImGuiCond_Appearing);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, EbDesktopMenuTheme::frameRounding());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, EbDesktopMenuTheme::panel());
        ImGui::PushStyleColor(ImGuiCol_Border, EbDesktopMenuTheme::outline());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, EbDesktopMenuTheme::surface());
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.18f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.22f, 0.22f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, EbDesktopMenuTheme::text());
        if (ImGui::BeginPopup("##eb-color-popup",
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGuiColorEditFlags const flags =
                ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayRGB |
                ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_NoSidePreview |
                ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha |
                ImGuiColorEditFlags_Uint8;
            if (ImGui::ColorPicker3("##eb-color-picker", draft, flags)) {
                clampRgb(draft);
                if (!nearlyEqual(draft, rgb)) {
                    rgb[0] = draft[0];
                    rgb[1] = draft[1];
                    rgb[2] = draft[2];
                    changed = true;
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(4);

        EbDesktopMenuFonts::pop();
        ImGui::PopID();
        return changed;
    }

private:
    EbColorPickerDraw() = delete;
};

bool EbColorPicker::draw(char const* id, char const* label, float rgb[3], bool enabled) {
    return EbColorPickerDraw::drawRow(id, label, rgb, enabled);
}

bool EbColorPicker::drawSetting(char const* key, char const* label, bool enabled) {
    if (!key || key[0] == '\0') {
        return false;
    }
    if (!EbDesktopMenuBind::exists(key)) {
        return false;
    }
    cocos2d::ccColor3B const current =
        EbDesktopMenuBind::getColor(key, cocos2d::ccColor3B{124, 255, 74});
    float rgb[3] = {current.r / 255.0f, current.g / 255.0f, current.b / 255.0f};
    if (!EbColorPickerDraw::drawRow(key, label, rgb, enabled)) {
        return false;
    }
    cocos2d::ccColor3B const next{
        static_cast<unsigned char>(rgb[0] * 255.0f + 0.5f),
        static_cast<unsigned char>(rgb[1] * 255.0f + 0.5f),
        static_cast<unsigned char>(rgb[2] * 255.0f + 0.5f),
    };
    return EbDesktopMenuBind::setColor(key, next);
}

#else

bool EbColorPicker::draw(char const* id, char const* label, float rgb[3], bool enabled) {
    (void)id;
    (void)label;
    (void)rgb;
    (void)enabled;
    return false;
}

bool EbColorPicker::drawSetting(char const* key, char const* label, bool enabled) {
    (void)key;
    (void)label;
    (void)enabled;
    return false;
}

#endif
