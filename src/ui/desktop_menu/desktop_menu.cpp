#include "desktop_menu.hpp"
#include "desktop_menu_bind.hpp"
#include "desktop_menu_fonts.hpp"
#include "desktop_menu_icons.hpp"
#include "desktop_menu_shell.hpp"
#include "desktop_menu_theme.hpp"
#include "widgets/eb_keybind.hpp"

#if defined(EB_DESKTOP_IMGUI)

#include <Geode/Geode.hpp>
#include <Geode/modify/CCMouseDispatcher.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <imgui-cocos.hpp>
#include <imgui.h>
#include <vector>

class EbDesktopMenuInput {
public:
    // Broader than imgui-cocos' cocosToImGuiKey (adds F-keys, Space, …) so Esc /
    // capture UI see the same edges while the menu owns the keyboard.
    static ImGuiKey toImGuiKey(cocos2d::enumKeyCodes key) {
        if (key >= cocos2d::KEY_A && key <= cocos2d::KEY_Z) {
            return static_cast<ImGuiKey>(ImGuiKey_A + (key - cocos2d::KEY_A));
        }
        if (key >= cocos2d::KEY_Zero && key <= cocos2d::KEY_Nine) {
            return static_cast<ImGuiKey>(ImGuiKey_0 + (key - cocos2d::KEY_Zero));
        }
        if (key >= cocos2d::KEY_F1 && key <= cocos2d::KEY_F12) {
            return static_cast<ImGuiKey>(ImGuiKey_F1 + (key - cocos2d::KEY_F1));
        }
        switch (key) {
        case cocos2d::KEY_Up:
            return ImGuiKey_UpArrow;
        case cocos2d::KEY_Down:
            return ImGuiKey_DownArrow;
        case cocos2d::KEY_Left:
            return ImGuiKey_LeftArrow;
        case cocos2d::KEY_Right:
            return ImGuiKey_RightArrow;
        case cocos2d::KEY_Control:
        case cocos2d::KEY_LeftControl:
            return ImGuiKey_LeftCtrl;
        case cocos2d::KEY_RightControl:
            return ImGuiKey_RightCtrl;
        case cocos2d::KEY_LeftWindowsKey:
            return ImGuiKey_LeftSuper;
        case cocos2d::KEY_RightWindowsKey:
            return ImGuiKey_RightSuper;
        case cocos2d::KEY_Shift:
        case cocos2d::KEY_LeftShift:
            return ImGuiKey_LeftShift;
        case cocos2d::KEY_RightShift:
            return ImGuiKey_RightShift;
        case cocos2d::KEY_Alt:
        case cocos2d::KEY_LeftMenu:
            return ImGuiKey_LeftAlt;
        case cocos2d::KEY_RightMenu:
            return ImGuiKey_RightAlt;
        case cocos2d::KEY_Enter:
            return ImGuiKey_Enter;
        case cocos2d::KEY_Home:
            return ImGuiKey_Home;
        case cocos2d::KEY_End:
            return ImGuiKey_End;
        case cocos2d::KEY_Delete:
            return ImGuiKey_Delete;
        case cocos2d::KEY_Backspace:
            return ImGuiKey_Backspace;
        case cocos2d::KEY_Tab:
            return ImGuiKey_Tab;
        case cocos2d::KEY_Escape:
            return ImGuiKey_Escape;
        case cocos2d::KEY_Space:
            return ImGuiKey_Space;
        case cocos2d::KEY_PageUp:
            return ImGuiKey_PageUp;
        case cocos2d::KEY_PageDown:
            return ImGuiKey_PageDown;
        case cocos2d::KEY_Insert:
            return ImGuiKey_Insert;
        default:
            return ImGuiKey_None;
        }
    }

    static void feedImGui(geode::KeyboardInputData const& data) {
        if (!ImGui::GetCurrentContext()) {
            return;
        }
        ImGuiIO& io = ImGui::GetIO();
        bool const down = data.action != geode::KeyboardInputData::Action::Release;
        io.AddKeyEvent(ImGuiMod_Ctrl,
                       static_cast<bool>(data.modifiers & geode::KeyboardModifier::Control));
        io.AddKeyEvent(ImGuiMod_Shift,
                       static_cast<bool>(data.modifiers & geode::KeyboardModifier::Shift));
        io.AddKeyEvent(ImGuiMod_Alt, static_cast<bool>(data.modifiers & geode::KeyboardModifier::Alt));
        io.AddKeyEvent(ImGuiMod_Super,
                       static_cast<bool>(data.modifiers & geode::KeyboardModifier::Super));
        ImGuiKey const imKey = toImGuiKey(data.key);
        if (imKey != ImGuiKey_None) {
            io.AddKeyEvent(imKey, down);
        }
    }

    static bool matchesDesktopMenuHotkey(geode::KeyboardInputData const& data) {
        if (data.action != geode::KeyboardInputData::Action::Press) {
            return false;
        }
        if (data.key == cocos2d::KEY_Escape) {
            return false;
        }
        // Canonicalize both sides — Geode stores RightShift with Shift bit set.
        geode::Keybind const pressed =
            EbKeybind::canonicalKeybind(data.key, data.modifiers);
        std::vector<geode::Keybind> const binds =
            EbDesktopMenuBind::getKeybind("hotkey-desktop-menu");
        for (geode::Keybind const& bind : binds) {
            if (EbKeybind::canonicalKeybind(bind) == pressed) {
                return true;
            }
        }
        return false;
    }

    static void repairStoredKeybinds() {
        char const* keys[] = {
            "hotkey-desktop-menu", "hotkey-debug",     "hotkey-reset-level",
            "hotkey-auto-tune",    "hotkey-assist-mode", "hotkey-trajectory",
        };
        for (char const* key : keys) {
            if (!EbDesktopMenuBind::exists(key)) {
                continue;
            }
            std::vector<geode::Keybind> binds = EbDesktopMenuBind::getKeybind(key);
            bool dirty = false;
            for (geode::Keybind& bind : binds) {
                geode::Keybind const canon = EbKeybind::canonicalKeybind(bind);
                if (!(canon == bind)) {
                    bind = canon;
                    dirty = true;
                }
            }
            if (dirty) {
                EbDesktopMenuBind::setKeybind(key, binds);
            }
        }
    }

    static void installKeyboard() {
        repairStoredKeybinds();
        // Only while wantsOpen (not close-fade): otherwise Geode never sees the
        // toggle press and the menu cannot reopen until the animation ends.
        // Capture + close still owned here because we Stop Geode listeners.
        geode::KeyboardInputEvent()
            .listen(
                [](geode::KeyboardInputData& data) -> geode::ListenerResult {
                    if (!EbDesktopMenuShell::wantsOpen()) {
                        return geode::ListenerResult::Propagate;
                    }
                    feedImGui(data);
                    if (EbKeybind::isCapturing()) {
                        EbKeybind::noteKeyboard(data);
                        return geode::ListenerResult::Stop;
                    }
                    if (matchesDesktopMenuHotkey(data)) {
                        EbDesktopMenu::setOpen(false);
                    }
                    return geode::ListenerResult::Stop;
                },
                geode::Priority::Early)
            .leak();
    }

private:
    EbDesktopMenuInput() = delete;
};

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
            // Fonts / icons first — setup re-runs after fullscreen toggles.
            EbDesktopMenuFonts::load();
            EbDesktopMenuIcons::load();
            EbDesktopMenuTheme::apply();
        })
        .draw([] {
            if (!ImGuiCocos::get().isVisible()) {
                return;
            }
            // Force capture flags while capturing so imgui-cocos also treats keys as UI.
            if (EbKeybind::isCapturing() && ImGui::GetCurrentContext()) {
                ImGui::SetNextFrameWantCaptureKeyboard(true);
            }
            EbDesktopMenuShell::draw();
            if (!EbDesktopMenuShell::shouldKeepVisible()) {
                ImGuiCocos::get().setVisible(false);
            }
        });
    // Closed until keybind / pause hub opens it.
    ImGuiCocos::get().setVisible(false);
    EbDesktopMenuShell::requestOpen(false);
    EbDesktopMenuInput::installKeyboard();
}

bool EbDesktopMenu::isOpen() {
    return EbDesktopMenuShell::wantsOpen() || EbDesktopMenuShell::shouldKeepVisible();
}

void EbDesktopMenu::setOpen(bool open) {
    if (open) {
        ImGuiCocos::get().setVisible(true);
    } else {
        EbKeybind::cancelCapture();
    }
    EbDesktopMenuShell::requestOpen(open);
    if (!open && !EbDesktopMenuShell::shouldKeepVisible()) {
        ImGuiCocos::get().setVisible(false);
    }
}

void EbDesktopMenu::toggle() {
    setOpen(!EbDesktopMenuShell::wantsOpen());
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
