#include "desktop_menu.hpp"
#include "desktop_menu_fonts.hpp"
#include "desktop_menu_shell.hpp"
#include "desktop_menu_theme.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include <Geode/Geode.hpp>
#include <Geode/modify/CCMouseDispatcher.hpp>
#include <imgui-cocos.hpp>

class $modify(EbDesktopMenuMouseDispatcher, cocos2d::CCMouseDispatcher) {
public:
    static void onModify(auto& self) {
        // Accumulate scroll before imgui-cocos may swallow WantCaptureMouse.
        (void)self.setHookPriority("cocos2d::CCMouseDispatcher::dispatchScrollMSG",
                                   geode::Priority::Early);
    }

    bool dispatchScrollMSG(float y, float x) {
        if (EbDesktopMenu::isOpen()) {
            // Raw Cocos delta → tab content host (not section). Sign: +y scrolls down.
            EbDesktopMenuShell::addWheelDelta(y);
        }
        return CCMouseDispatcher::dispatchScrollMSG(y, x);
    }

private:
};

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
            // Fonts first — setup re-runs after fullscreen toggles.
            EbDesktopMenuFonts::load();
            EbDesktopMenuTheme::apply();
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
