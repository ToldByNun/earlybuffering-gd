#ifndef DESKTOP_MENU_CLOUD_OVERLAY_HPP
#define DESKTOP_MENU_CLOUD_OVERLAY_HPP

// Dimmed ImGui Cloud Configs overlay above the desktop shell (AP1+).
// 1.6.0: unbound — openCloudConfigs uses classic EbCloudConfigPopup; keep this
// for a later ship. Esc dismisses share first, then overlay. Drawn under modal.
class EbDesktopCloudOverlay {
public:
    static void open();
    static void dismiss();

    static bool isOpen();
    // True while visible or closing — host keeps ImGui up if needed.
    static bool shouldKeepVisible();

    // Cocos wheel while the cloud sheet owns the menu (shell is suppressed).
    static void addWheelDelta(float deltaY);

    // Call after the shell, before the modal, each ImGui frame.
    static void draw();

private:
    EbDesktopCloudOverlay() = delete;
};

#endif
