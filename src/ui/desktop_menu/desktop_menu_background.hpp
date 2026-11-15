#ifndef DESKTOP_MENU_BACKGROUND_HPP
#define DESKTOP_MENU_BACKGROUND_HPP
#if defined(EB_DESKTOP_IMGUI)
#include <imgui.h>
class EbDesktopMenuBackground {
public:
    static void paint(float openAlpha);
    static ImVec4 dim(float openAlpha);
    static void settings();

private:
};
#endif
#endif
