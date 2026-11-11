#include "desktop_menu.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include <Geode/Geode.hpp>
#include <imgui-cocos.hpp>
#include <imgui.h>

bool EbDesktopMenu::isSupported() {
    return true;
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
            // AP1 stub only — shell / widgets arrive in later APs.
            ImGui::SetNextWindowSize(ImVec2(320.0f, 160.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("EarlyBuffer Desktop Menu", nullptr)) {
                ImGui::TextUnformatted("ImGuiCocos desktop gate OK (AP1 stub)");
            }
            ImGui::End();
        });
    // Closed until AP2 keybind / pause hub opens it.
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

void EbDesktopMenu::install() {}

bool EbDesktopMenu::isOpen() {
    return false;
}

void EbDesktopMenu::setOpen(bool) {}

void EbDesktopMenu::toggle() {}

#endif
