#ifndef DESKTOP_MENU_PANELS_HPP
#define DESKTOP_MENU_PANELS_HPP

#include "../settings/in_level_settings_filter.hpp"

// Per-tab SettingV3 panels for the desktop ImGui shell.
class EbDesktopMenuPanels {
public:
    static void draw(SettingsCategory category);

private:
    EbDesktopMenuPanels() = delete;
};

#endif
