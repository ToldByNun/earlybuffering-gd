#ifndef DESKTOP_MENU_SIDEBAR_SMOKE_HPP
#define DESKTOP_MENU_SIDEBAR_SMOKE_HPP

#if defined(EB_DESKTOP_IMGUI)
#include <imgui.h>
#endif

// Sidebar wave fill. Uses ImDrawList sampling of the Shadertoy math — no raw GL/FBO
// (GD crashed when we drove custom framebuffers from the ImGui swap path).
class EbDesktopMenuSidebarSmoke {
public:
    static EbDesktopMenuSidebarSmoke& get();

#if defined(EB_DESKTOP_IMGUI)
    // Draw the animated wave into a rounded rect. cornerFlags selects which
    // corners stay rounded (docked shell uses left-only).
    // outsideColor punches corner remask; 0 = shell bg().
    void paint(ImDrawList* drawList, ImVec2 const& min, ImVec2 const& max, float rounding,
               float timeSeconds, ImDrawFlags cornerFlags = ImDrawFlags_RoundCornersLeft,
               ImU32 outsideColor = 0);

    // Single wave sample (same math as the sidebar shader) — for solid widget fills.
    static ImU32 sampleColor(float timeSeconds, float px = 0.5f, float py = 0.5f);
#endif

private:
    EbDesktopMenuSidebarSmoke() = default;
};

#endif
