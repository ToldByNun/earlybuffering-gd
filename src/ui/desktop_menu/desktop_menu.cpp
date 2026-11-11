#include "desktop_menu.hpp"
#include "desktop_menu_shell.hpp"
#include "desktop_menu_theme.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include <Geode/Geode.hpp>
#include <imgui-cocos.hpp>

bool EbDesktopMenu::isSupported() {
    return true;
}

bool EbDesktopMenu::useImGuiShell() {
    auto* mod = geode::prelude::Mod::get();
    if (!mod) {
        return false;
    }
    return !mod->getSettingValue<bool>("desktop-classic-settings");
}

void EbDesktopMenu::install() {
    ImGuiCocos::get()
        .setup([] {
            EbDesktopMenuTheme::apply();
            // Fonts land in AP5.
        })
        .draw([] {
            if (!ImGuiCocos::get().isVisible()) {
                return;
            }
            EbDesktopMenuShell::draw();
        });
    // Closed until keybind / pause hub opens it.
    ImGuiCocos::get().setVisible(false);
}

bool EbDesktopMenu::isOpen() {
    return ImGuiCocos::get().isVisible();
}

void EbDesktopMenu::setOpen(bool open) {
    ImGuiCocos::get().setVisible(open);
}

void EbDesktopMenu::toggle() {
    ImGuiCocos::get().toggle();
}

#else

bool EbDesktopMenu::isSupported() {
    return false;
}

bool EbDesktopMenu::useImGuiShell() {
    return false;
}

void EbDesktopMenu::install() {}

bool EbDesktopMenu::isOpen() {
    return false;
}

void EbDesktopMenu::setOpen(bool) {}

void EbDesktopMenu::toggle() {}

#endif
