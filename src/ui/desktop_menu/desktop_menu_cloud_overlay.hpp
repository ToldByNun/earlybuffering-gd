#ifndef DESKTOP_MENU_CLOUD_OVERLAY_HPP
#define DESKTOP_MENU_CLOUD_OVERLAY_HPP

// Dimmed ImGui Cloud Configs overlay above the desktop shell (AP1+).
// Esc dismisses (share panel first once AP7 lands). Drawn under EbDesktopMenuModal.
class EbDesktopCloudOverlay {
public:
    static void open();
    static void dismiss();

    static bool isOpen();
    // True while visible or closing — host keeps ImGui up if needed.
    static bool shouldKeepVisible();

    // Call after the shell, before the modal, each ImGui frame.
    static void draw();

private:
    EbDesktopCloudOverlay() = delete;
};

#endif
