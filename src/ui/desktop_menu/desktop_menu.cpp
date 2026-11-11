#include "desktop_menu.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include <Geode/Geode.hpp>
#include <imgui-cocos.hpp>
#include <imgui.h>

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
            // Theme + fonts land in later APs.
        })
        .draw([] {
            if (!ImGuiCocos::get().isVisible()) {
                return;
            }
            // AP1/AP2 stub only — shell / widgets arrive in later APs.
            ImGui::SetNextWindowSize(ImVec2(320.0f, 160.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("EarlyBuffer Desktop Menu", nullptr)) {
                ImGui::TextUnformatted("ImGuiCocos desktop gate OK (AP2 stub)");
                ImGui::TextUnformatted("Classic fallback: Main → Classic Settings Menu");
            }
            ImGui::End();
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
