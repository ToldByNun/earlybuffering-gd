#ifndef DESKTOP_MENU_ICONS_HPP
#define DESKTOP_MENU_ICONS_HPP

#if defined(EB_DESKTOP_IMGUI)
#include <imgui.h>
#endif

// Desktop ImGui shell icons: sidebar links + Modes gamemode picker bar.
class EbDesktopMenuIcons {
public:
    // Cube → Swing order used by the Modes tab picker.
    static constexpr int kGamemodeCount = 8;

#if defined(EB_DESKTOP_IMGUI)
    // Load Cocos textures (safe to call again after fullscreen — reuses cache).
    static void load();
    // Bottom-left link buttons inside the sidebar bounds.
    static void drawSidebarLinks(ImVec2 const& sideMin, float sideWidth, float sideHeight);
    // Horizontal gamemode icon row. Writes 0..kGamemodeCount-1 into *selected.
    // Returns true when the selection changed.
    static bool drawGamemodeBar(int* selected);
#endif

    static char const* gamemodeTitle(int index);
    static char const* gamemodePrefix(int index);

private:
    EbDesktopMenuIcons() = delete;
};

#endif
