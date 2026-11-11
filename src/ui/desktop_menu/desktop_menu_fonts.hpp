#ifndef DESKTOP_MENU_FONTS_HPP
#define DESKTOP_MENU_FONTS_HPP

#if defined(EB_DESKTOP_IMGUI)
#include <imgui.h>
#endif

// Inter TTF faces for the desktop ImGui shell (UI / header / caps).
class EbDesktopMenuFonts {
public:
    static void load();

    static float uiSize();
    static float headerSize();
    static float capsSize();

#if defined(EB_DESKTOP_IMGUI)
    static ImFont* ui();
    static ImFont* header();
    static ImFont* caps();
    static void pushUi();
    static void pushHeader();
    static void pushCaps();
    static void pop();
#endif

private:
    EbDesktopMenuFonts() = delete;
};

#endif
