#ifndef DESKTOP_MENU_ICONS_HPP
#define DESKTOP_MENU_ICONS_HPP

#if defined(EB_DESKTOP_IMGUI)
#include <imgui.h>
#endif

// Sidebar footer icons (GitHub / Discord) for the desktop ImGui shell.
class EbDesktopMenuIcons {
public:
#if defined(EB_DESKTOP_IMGUI)
    // Load Cocos textures (safe to call again after fullscreen — reuses cache).
    static void load();
    // Bottom-left link buttons inside the sidebar bounds.
    static void drawSidebarLinks(ImVec2 const& sideMin, float sideWidth, float sideHeight);
#endif

private:
    EbDesktopMenuIcons() = delete;
};

#endif
