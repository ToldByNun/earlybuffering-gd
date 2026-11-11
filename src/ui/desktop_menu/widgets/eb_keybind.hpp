#ifndef EB_KEYBIND_HPP
#define EB_KEYBIND_HPP

#include <Geode/Geode.hpp>
#include <vector>

// Label + bind field; click to capture a Geode keybind (Esc cancels, Backspace clears).
class EbKeybind {
public:
    // True while any keybind row is waiting for a key (hotkey listeners should no-op).
    static bool isCapturing();

    // Returns true when *binds changes. Empty vector = unbound.
    static bool draw(char const* id, char const* label, std::vector<geode::Keybind>* binds,
                     bool enabled = true);

    // Binds a keybind SettingV3 key (e.g. hotkey-desktop-menu).
    static bool drawSetting(char const* key, char const* label, bool enabled = true);

private:
    EbKeybind() = delete;
};

#endif
