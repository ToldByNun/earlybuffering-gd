#ifndef EB_KEYBIND_HPP
#define EB_KEYBIND_HPP

#include <Geode/Geode.hpp>
#include <vector>

// Label + bind field; click to capture a Geode keybind (Esc cancels, Backspace clears).
// Capture is driven by Geode KeyboardInputEvent (not ImGui alone) so keys bind even when
// imgui-cocos would skip AddKeyEvent without WantCaptureKeyboard.
class EbKeybind {
public:
    // True while any keybind row is waiting for a key (hotkey listeners should no-op).
    static bool isCapturing();
    // Drop capture without writing (Esc / menu close / click-away).
    static void cancelCapture();

    // Raw Geode keyboard event while the desktop menu is open (from desktop_menu.cpp).
    static void noteKeyboard(geode::KeyboardInputData const& data);

    // Live chord preview while capturing ("Ctrl+Shift+D", or "Press a key…").
    static char const* captureHint();

    // Match Geode Keybind::fromString / live KeyboardInputEvent: modifier keys
    // always carry their own mod bit (RightShift → Shift, etc.).
    static geode::Keybind canonicalKeybind(cocos2d::enumKeyCodes key,
                                           geode::KeyboardModifier mods);
    static geode::Keybind canonicalKeybind(geode::Keybind const& bind);

    // Returns true when *binds changes. Empty vector = unbound.
    static bool draw(char const* id, char const* label, std::vector<geode::Keybind>* binds,
                     bool enabled = true, char const* tooltip = nullptr);

    // Binds a keybind SettingV3 key (e.g. hotkey-desktop-menu).
    // Tooltip defaults to the setting's mod.json description.
    static bool drawSetting(char const* key, char const* label, bool enabled = true);

private:
    EbKeybind() = delete;
};

#endif
