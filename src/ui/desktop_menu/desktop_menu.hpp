#ifndef DESKTOP_MENU_HPP
#define DESKTOP_MENU_HPP

// Desktop-only ImGui settings shell (Windows + macOS). Mobile builds compile
// empty stubs and never link gd-imgui-cocos.
class EbDesktopMenu {
public:
    static bool isSupported();
    static void install();
    static bool isOpen();
    static void setOpen(bool open);
    static void toggle();

private:
    EbDesktopMenu() = delete;
};

#endif
