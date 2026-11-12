#include "desktop_menu_theme.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include "desktop_menu_bind.hpp"
#include <algorithm>
#include <cmath>

class EbDesktopMenuThemeColors {
public:
    static ImVec4 rgbByte(int red, int green, int blue, float alpha = 1.0f) {
        return ImVec4(static_cast<float>(red) / 255.0f, static_cast<float>(green) / 255.0f,
                      static_cast<float>(blue) / 255.0f, alpha);
    }

    static ImVec4 fromSetting() {
        // Default matches historical EarlyBuffer purple (#7B61FF).
        cocos2d::ccColor3B const fallback{123, 97, 255};
        cocos2d::ccColor3B const color =
            EbDesktopMenuBind::getColor("desktop-accent-color", fallback);
        return rgbByte(color.r, color.g, color.b);
    }

    static ImVec4 lighten(ImVec4 const& color, float amount) {
        float const t = std::clamp(amount, 0.0f, 1.0f);
        return ImVec4(color.x + (1.0f - color.x) * t, color.y + (1.0f - color.y) * t,
                      color.z + (1.0f - color.z) * t, color.w);
    }

    static ImVec4 darken(ImVec4 const& color, float amount) {
        float const scale = 1.0f - std::clamp(amount, 0.0f, 1.0f);
        return ImVec4(color.x * scale, color.y * scale, color.z * scale, color.w);
    }

    static ImVec4 lerp(ImVec4 const& from, ImVec4 const& to, float t) {
        float const clamped = std::clamp(t, 0.0f, 1.0f);
        return ImVec4(from.x + (to.x - from.x) * clamped, from.y + (to.y - from.y) * clamped,
                      from.z + (to.z - from.z) * clamped, from.w + (to.w - from.w) * clamped);
    }

private:
    EbDesktopMenuThemeColors() = delete;
};

ImU32 EbDesktopMenuTheme::toU32(ImVec4 const& color) {
    return ImGui::ColorConvertFloat4ToU32(color);
}

ImVec4 EbDesktopMenuTheme::bg() {
    // Near-black canvas with a soft accent pull so secondary greys track the picker.
    ImVec4 const neutral = EbDesktopMenuThemeColors::rgbByte(13, 13, 18);
    return EbDesktopMenuThemeColors::lerp(neutral, EbDesktopMenuThemeColors::darken(accent(), 0.90f),
                                          0.14f);
}

ImVec4 EbDesktopMenuTheme::panel() {
    ImVec4 const neutral = EbDesktopMenuThemeColors::rgbByte(28, 28, 40);
    return EbDesktopMenuThemeColors::lerp(neutral, EbDesktopMenuThemeColors::darken(accent(), 0.82f),
                                          0.18f);
}

ImVec4 EbDesktopMenuTheme::surface() {
    ImVec4 const neutral = EbDesktopMenuThemeColors::rgbByte(26, 26, 30);
    return EbDesktopMenuThemeColors::lerp(neutral, EbDesktopMenuThemeColors::darken(accent(), 0.86f),
                                          0.16f);
}

ImVec4 EbDesktopMenuTheme::accent() {
    return EbDesktopMenuThemeColors::fromSetting();
}

ImVec4 EbDesktopMenuTheme::accentLight() {
    // Slightly lifted accent for fills / active chrome.
    return EbDesktopMenuThemeColors::lighten(accent(), 0.12f);
}

ImVec4 EbDesktopMenuTheme::sidebarBg() {
    // Deep dock base under the wave (readable white labels).
    return EbDesktopMenuThemeColors::darken(accent(), 0.52f);
}

ImVec4 EbDesktopMenuTheme::sidebarWaveDark() {
    // Matches shaders/sidebar_wave.frag base — between dock and accent.
    return EbDesktopMenuThemeColors::lerp(sidebarBg(), accent(), 0.72f);
}

ImVec4 EbDesktopMenuTheme::sidebarWaveLight() {
    return accentLight();
}

ImVec4 EbDesktopMenuTheme::text() {
    return EbDesktopMenuThemeColors::rgbByte(255, 255, 255);
}

ImVec4 EbDesktopMenuTheme::muted() {
    ImVec4 const neutral = EbDesktopMenuThemeColors::rgbByte(156, 163, 175);
    return EbDesktopMenuThemeColors::lerp(neutral, EbDesktopMenuThemeColors::lighten(accent(), 0.55f),
                                          0.22f);
}

ImVec4 EbDesktopMenuTheme::outline() {
    ImVec4 const neutral = EbDesktopMenuThemeColors::rgbByte(55, 55, 68);
    return EbDesktopMenuThemeColors::lerp(neutral, EbDesktopMenuThemeColors::darken(accent(), 0.55f),
                                          0.28f);
}

ImVec4 EbDesktopMenuTheme::danger() {
    return EbDesktopMenuThemeColors::rgbByte(239, 68, 68);
}

float EbDesktopMenuTheme::windowRounding() {
    return 20.0f;
}

float EbDesktopMenuTheme::panelRounding() {
    return 14.0f;
}

float EbDesktopMenuTheme::frameRounding() {
    return 12.0f;
}

float EbDesktopMenuTheme::pillRounding() {
    return 999.0f;
}

float EbDesktopMenuTheme::outlineThickness() {
    return 1.0f;
}

void EbDesktopMenuTheme::drawAccentGlow(ImDrawList* drawList, ImVec2 const& min, ImVec2 const& max,
                                        float rounding, float strength) {
    if (!drawList) {
        return;
    }
    float const clamped = std::clamp(strength, 0.0f, 2.0f);
    if (clamped <= 0.001f) {
        return;
    }
    ImVec4 glow = accent();
    float const pads[3] = {2.0f, 5.0f, 9.0f};
    float const alphas[3] = {0.35f, 0.18f, 0.08f};
    for (int index = 0; index < 3; ++index) {
        float const pad = pads[index] * clamped;
        glow.w = alphas[index] * clamped;
        drawList->AddRectFilled(ImVec2(min.x - pad, min.y - pad), ImVec2(max.x + pad, max.y + pad),
                                toU32(glow), rounding + pad);
    }
}

void EbDesktopMenuTheme::drawTextShadowed(ImDrawList* drawList, ImFont* font, float fontSize,
                                          ImVec2 const& pos, ImVec4 const& color,
                                          char const* text) {
    if (!drawList || !font || !text || text[0] == '\0' || fontSize <= 0.0f) {
        return;
    }
    float const alpha = std::clamp(color.w, 0.0f, 1.0f);
    if (alpha <= 0.01f) {
        return;
    }
    // Minimal 1px outer drop — nearly invisible on dark wave, just enough on bright peaks.
    ImU32 const shadow = toU32(ImVec4(0.0f, 0.0f, 0.0f, 0.28f * alpha));
    drawList->AddText(font, fontSize, ImVec2(pos.x + 1.0f, pos.y + 1.0f), shadow, text);
    drawList->AddText(font, fontSize, pos, toU32(color), text);
}

void EbDesktopMenuTheme::apply() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = windowRounding();
    style.ChildRounding = panelRounding();
    style.FrameRounding = frameRounding();
    style.PopupRounding = frameRounding();
    style.ScrollbarRounding = frameRounding();
    style.GrabRounding = pillRounding();
    style.TabRounding = frameRounding();

    style.WindowBorderSize = outlineThickness();
    style.ChildBorderSize = outlineThickness();
    style.FrameBorderSize = outlineThickness();
    style.PopupBorderSize = outlineThickness();

    style.WindowPadding = ImVec2(16.0f, 16.0f);
    style.FramePadding = ImVec2(14.0f, 10.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 12.0f;

    ImVec4 const cBg = bg();
    ImVec4 const cPanel = panel();
    ImVec4 const cSurface = surface();
    ImVec4 const cAccent = accent();
    ImVec4 const cAccentLight = accentLight();
    ImVec4 const cText = text();
    ImVec4 const cMuted = muted();
    ImVec4 const cOutline = outline();

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = cText;
    colors[ImGuiCol_TextDisabled] = ImVec4(cMuted.x, cMuted.y, cMuted.z, 0.55f);
    colors[ImGuiCol_WindowBg] = cBg;
    colors[ImGuiCol_ChildBg] = cPanel;
    colors[ImGuiCol_PopupBg] = cPanel;
    colors[ImGuiCol_Border] = cOutline;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg] = cSurface;
    colors[ImGuiCol_FrameBgHovered] =
        ImVec4(cSurface.x + 0.04f, cSurface.y + 0.04f, cSurface.z + 0.05f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(cAccent.x, cAccent.y, cAccent.z, 0.35f);
    colors[ImGuiCol_TitleBg] = cBg;
    colors[ImGuiCol_TitleBgActive] = cBg;
    colors[ImGuiCol_TitleBgCollapsed] = cBg;
    colors[ImGuiCol_MenuBarBg] = cPanel;
    colors[ImGuiCol_ScrollbarBg] = cBg;
    colors[ImGuiCol_ScrollbarGrab] = cOutline;
    colors[ImGuiCol_ScrollbarGrabHovered] = cMuted;
    colors[ImGuiCol_ScrollbarGrabActive] = cAccent;
    colors[ImGuiCol_CheckMark] = cText;
    colors[ImGuiCol_SliderGrab] = cText;
    colors[ImGuiCol_SliderGrabActive] = cAccentLight;
    colors[ImGuiCol_Button] = cAccent;
    colors[ImGuiCol_ButtonHovered] = cAccentLight;
    colors[ImGuiCol_ButtonActive] = EbDesktopMenuThemeColors::darken(cAccent, 0.18f);
    colors[ImGuiCol_Header] = ImVec4(cAccent.x, cAccent.y, cAccent.z, 0.35f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(cAccent.x, cAccent.y, cAccent.z, 0.55f);
    colors[ImGuiCol_HeaderActive] = cAccent;
    colors[ImGuiCol_Separator] = cOutline;
    colors[ImGuiCol_SeparatorHovered] = cAccent;
    colors[ImGuiCol_SeparatorActive] = cAccentLight;
    colors[ImGuiCol_ResizeGrip] = ImVec4(cAccent.x, cAccent.y, cAccent.z, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(cAccent.x, cAccent.y, cAccent.z, 0.55f);
    colors[ImGuiCol_ResizeGripActive] = cAccent;
    colors[ImGuiCol_Tab] = cPanel;
    colors[ImGuiCol_TabHovered] = ImVec4(cAccent.x, cAccent.y, cAccent.z, 0.45f);
    colors[ImGuiCol_TabSelected] = cAccent;
    colors[ImGuiCol_TabDimmed] = cPanel;
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(cAccent.x, cAccent.y, cAccent.z, 0.55f);
    colors[ImGuiCol_PlotLines] = cAccentLight;
    colors[ImGuiCol_PlotLinesHovered] = cAccent;
    colors[ImGuiCol_PlotHistogram] = cAccent;
    colors[ImGuiCol_PlotHistogramHovered] = cAccentLight;
    colors[ImGuiCol_TableHeaderBg] = cPanel;
    colors[ImGuiCol_TableBorderStrong] = cOutline;
    colors[ImGuiCol_TableBorderLight] = ImVec4(cOutline.x, cOutline.y, cOutline.z, 0.45f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(cAccent.x, cAccent.y, cAccent.z, 0.35f);
    colors[ImGuiCol_DragDropTarget] = cAccentLight;
    colors[ImGuiCol_NavCursor] = cAccent;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(cAccent.x, cAccent.y, cAccent.z, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
}

#else

void EbDesktopMenuTheme::apply() {}

#endif
