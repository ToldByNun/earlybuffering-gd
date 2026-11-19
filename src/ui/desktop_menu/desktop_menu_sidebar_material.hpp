#ifndef DESKTOP_MENU_SIDEBAR_MATERIAL_HPP
#define DESKTOP_MENU_SIDEBAR_MATERIAL_HPP
#if defined(EB_DESKTOP_IMGUI)
#include <imgui.h>
class EbDesktopMenuSidebarMaterial {
public:
    enum class Parent { Background = 0, Sidebar = 1 };
    enum class Layer { Classic = 0, Blur = 1, LiquidGlass = 2 };
    static void paint(ImDrawList* list, ImVec2 min, ImVec2 max, float rounding);
    static void settings();
    static ImVec4 color(bool light, ImVec4 fallback);

private:
};
#endif
#endif
