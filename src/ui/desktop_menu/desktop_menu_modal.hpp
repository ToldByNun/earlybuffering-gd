#ifndef DESKTOP_MENU_MODAL_HPP
#define DESKTOP_MENU_MODAL_HPP

#include <functional>
#include <string>

// Dimmed ImGui modal drawn above the desktop shell (AP7).
// Esc dismisses (info = OK, confirm = Cancel/onNo).
class EbDesktopMenuModal {
public:
    static void showInfo(std::string title, std::string body);
    static void showConfirm(std::string title, std::string body, std::function<void()> onYes,
                            std::function<void()> onNo = {}, char const* yesLabel = "Yes",
                            char const* noLabel = "Cancel");

    static bool isOpen();
    // True while visible or closing — host keeps ImGui up if needed.
    static bool shouldKeepVisible();

    // Call after the shell each ImGui frame.
    static void draw();

    static void dismiss();

private:
    EbDesktopMenuModal() = delete;
};

#endif
