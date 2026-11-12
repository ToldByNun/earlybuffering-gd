#ifndef DESKTOP_MENU_SHELL_HPP
#define DESKTOP_MENU_SHELL_HPP

#include "../settings/in_level_settings_filter.hpp"

// Image-2 chrome: top bar, sidebar nav, content host, category switch.
// Desktop draws ImGui; mobile is a no-op stub.
class EbDesktopMenuShell {
public:
    static void draw();
    static SettingsCategory activeCategory();
    static void setActiveCategory(SettingsCategory category);

    // Open/close animation — keep ImGui visible until the close ease finishes.
    static void requestOpen(bool open);
    static bool wantsOpen();
    static bool shouldKeepVisible();

    // Cocos mouse-wheel delta while the menu is open (content host consumes it).
    static void addWheelDelta(float deltaY);
    static float takeWheelDelta();

    // Combo / dropdown / color popups are viewport-anchored. Shell requests dismiss on
    // scroll/tab; widgets honor it inside BeginPopup (CloseCurrentPopup only works there).
    static void requestDismissFloatingPopups();
    static bool shouldDismissFloatingPopups();

private:
    EbDesktopMenuShell() = delete;
};

#endif
