#ifndef DESKTOP_MENU_ALERT_HPP
#define DESKTOP_MENU_ALERT_HPP

#include <functional>
#include <string>

// Routes action feedback to the desktop ImGui modal when the shell is up,
// otherwise falls back to FLAlertLayer / createQuickPopup (mobile / classic).
class EbDesktopAlert {
public:
    static void info(std::string const& title, std::string const& body);
    static void info(char const* title, char const* body);

    static void confirm(std::string const& title, std::string const& body,
                        std::function<void()> onYes, std::function<void()> onNo = {},
                        char const* yesLabel = "Yes", char const* noLabel = "Cancel");

private:
    EbDesktopAlert() = delete;
};

#endif
