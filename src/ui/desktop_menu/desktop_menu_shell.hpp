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

private:
    EbDesktopMenuShell() = delete;
};

#endif
