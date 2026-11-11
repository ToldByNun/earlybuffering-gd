#include "desktop_menu.hpp"
#include "desktop_menu_theme.hpp"

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
            EbDesktopMenuTheme::apply();
            // Fonts land in AP5.
        })
        .draw([] {
            if (!ImGuiCocos::get().isVisible()) {
                return;
            }
            // AP3 stub — shell / widgets arrive in later APs.
            ImGui::SetNextWindowSize(ImVec2(360.0f, 200.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("EarlyBuffer Desktop Menu", nullptr)) {
                ImGui::TextUnformatted("Theme tokens applied (AP3)");
                ImGui::TextColored(EbDesktopMenuTheme::muted(),
                                   "Classic fallback: Main → Classic Settings Menu");
                ImGui::Spacing();
                ImVec2 const btnMin = ImGui::GetCursorScreenPos();
                float const btnW = ImGui::GetContentRegionAvail().x;
                float const btnH = ImGui::GetFrameHeight();
                EbDesktopMenuTheme::drawAccentGlow(
                    ImGui::GetWindowDrawList(), btnMin, ImVec2(btnMin.x + btnW, btnMin.y + btnH),
                    EbDesktopMenuTheme::frameRounding(), 0.85f);
                if (ImGui::Button("Accent sample", ImVec2(-1.0f, 0.0f))) {
                }
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
