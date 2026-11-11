#ifndef DESKTOP_MENU_THEME_HPP
#define DESKTOP_MENU_THEME_HPP

#if defined(EB_DESKTOP_IMGUI)
#include <imgui.h>
#endif

// Image-1 palette + rounding for the desktop ImGui shell.
// Mobile builds keep empty stubs (no imgui link).
class EbDesktopMenuTheme {
public:
    static void apply();

#if defined(EB_DESKTOP_IMGUI)
    static ImU32 toU32(ImVec4 const& color);
    static ImVec4 bg();
    static ImVec4 panel();
    static ImVec4 surface();
    static ImVec4 sidebarBg();
    static ImVec4 accent();
    static ImVec4 accentLight();
    static ImVec4 text();
    static ImVec4 muted();
    static ImVec4 outline();
    static ImVec4 danger();

    static float windowRounding();
    static float panelRounding();
    static float frameRounding();
    static float pillRounding();
    static float outlineThickness();

    // Soft purple bloom behind an active pill / focused control.
    static void drawAccentGlow(ImDrawList* drawList, ImVec2 const& min, ImVec2 const& max,
                               float rounding, float strength = 1.0f);
#endif

private:
    EbDesktopMenuTheme() = delete;
};

#endif
