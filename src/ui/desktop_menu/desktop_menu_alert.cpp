#include "desktop_menu_alert.hpp"
#include "desktop_menu.hpp"
#include "desktop_menu_modal.hpp"
#include <Geode/Geode.hpp>

void EbDesktopAlert::info(std::string const& title, std::string const& body) {
#if defined(EB_DESKTOP_IMGUI)
    if (EbDesktopMenu::useImGuiShell() && EbDesktopMenu::isOpen()) {
        EbDesktopMenuModal::showInfo(title, body);
        return;
    }
#endif
    FLAlertLayer::create(title.c_str(), body.c_str(), "OK")->show();
}

void EbDesktopAlert::info(char const* title, char const* body) {
    info(std::string(title ? title : "EarlyBuffer"), std::string(body ? body : ""));
}

void EbDesktopAlert::confirm(std::string const& title, std::string const& body,
                             std::function<void()> onYes, std::function<void()> onNo,
                             char const* yesLabel, char const* noLabel) {
    char const* yes = yesLabel && yesLabel[0] != '\0' ? yesLabel : "Yes";
    char const* no = noLabel && noLabel[0] != '\0' ? noLabel : "Cancel";
#if defined(EB_DESKTOP_IMGUI)
    if (EbDesktopMenu::useImGuiShell() && EbDesktopMenu::isOpen()) {
        (void)yes;
        (void)no;
        EbDesktopMenuModal::showConfirm(title, body, std::move(onYes), std::move(onNo));
        return;
    }
#endif
    geode::prelude::createQuickPopup(
        title.c_str(), body.c_str(), no, yes,
        [onYes = std::move(onYes), onNo = std::move(onNo)](auto*, bool btn2) {
            if (btn2) {
                if (onYes) {
                    onYes();
                }
            } else if (onNo) {
                onNo();
            }
        });
}
