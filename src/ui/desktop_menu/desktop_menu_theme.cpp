#include "desktop_menu_theme.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include <algorithm>

class EbDesktopMenuThemeColors {
public:
    static ImVec4 rgbByte(int red, int green, int blue, float alpha = 1.0f) {
        return ImVec4(static_cast<float>(red) / 255.0f, static_cast<float>(green) / 255.0f,
                      static_cast<float>(blue) / 255.0f, alpha);
    }

private:
    EbDesktopMenuThemeColors() = delete;
};

ImU32 EbDesktopMenuTheme::toU32(ImVec4 const& color) {
    return ImGui::ColorConvertFloat4ToU32(color);
}

ImVec4 EbDesktopMenuTheme::bg() {
    // Near-black canvas (image 1 right pane).
    return EbDesktopMenuThemeColors::rgbByte(13, 13, 18);
}

ImVec4 EbDesktopMenuTheme::panel() {
    // Section card — lifted enough that tab bg() reads as a gutter around it.
    return EbDesktopMenuThemeColors::rgbByte(28, 28, 40);
}

ImVec4 EbDesktopMenuTheme::surface() {
    // Input / control wells.
    return EbDesktopMenuThemeColors::rgbByte(26, 26, 30);
}

ImVec4 EbDesktopMenuTheme::sidebarBg() {
    // Deep purple — readable white labels (flat stand-in until AP6 shader).
    return EbDesktopMenuThemeColors::rgbByte(58, 42, 148);
}

ImVec4 EbDesktopMenuTheme::accent() {
    // Primary purple (button / active pill / sidebar base).
    return EbDesktopMenuThemeColors::rgbByte(123, 97, 255);
}

ImVec4 EbDesktopMenuTheme::accentLight() {
    // Shader lightWave — lighter purple for fills / gradients.
    return EbDesktopMenuThemeColors::rgbByte(131, 112, 255);
}

ImVec4 EbDesktopMenuTheme::text() {
    return EbDesktopMenuThemeColors::rgbByte(255, 255, 255);
}

ImVec4 EbDesktopMenuTheme::muted() {
    // Caps labels / descriptions.
    return EbDesktopMenuThemeColors::rgbByte(156, 163, 175);
}

ImVec4 EbDesktopMenuTheme::outline() {
    // Thin grey stroke on inputs / cards.
    return EbDesktopMenuThemeColors::rgbByte(55, 55, 68);
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
    // Three expanding soft rings — cheap bloom without a post-process pass.
    float const pads[3] = {2.0f, 5.0f, 9.0f};
    float const alphas[3] = {0.35f, 0.18f, 0.08f};
    for (int index = 0; index < 3; ++index) {
        float const pad = pads[index] * clamped;
        glow.w = alphas[index] * clamped;
        drawList->AddRectFilled(ImVec2(min.x - pad, min.y - pad), ImVec2(max.x + pad, max.y + pad),
                                toU32(glow), rounding + pad);
    }
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
    colors[ImGuiCol_ButtonActive] = ImVec4(0.40f, 0.28f, 0.95f, 1.0f);
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
