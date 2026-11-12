#ifndef DESKTOP_MENU_BIND_HPP
#define DESKTOP_MENU_BIND_HPP

#include <Geode/Geode.hpp>
#include <cstdint>
#include <string>
#include <vector>

// Typed SettingV3 get/set for the desktop ImGui shell.
// Geode Mod settings remain the only store — no parallel cache of widget values.
// Writes use setSettingValue (fires existing listenForSettingChanges) and then
// refresh Stage/Dev caches so getters match immediately.
class EbDesktopMenuBind {
public:
    static bool exists(char const* key);

    static bool getBool(char const* key, bool fallback = false);
    // true when the store value changed.
    static bool setBool(char const* key, bool value);

    static int64_t getInt(char const* key, int64_t fallback = 0);
    static bool setInt(char const* key, int64_t value);

    static double getFloat(char const* key, double fallback = 0.0);
    static bool setFloat(char const* key, double value);

    static std::string getString(char const* key, std::string const& fallback = std::string());
    static bool setString(char const* key, std::string const& value);

    static cocos2d::ccColor3B getColor(char const* key,
                                       cocos2d::ccColor3B fallback = cocos2d::ccColor3B{255, 255,
                                                                                       255});
    static bool setColor(char const* key, cocos2d::ccColor3B value);

    static std::vector<geode::Keybind> getKeybind(char const* key);
    static bool setKeybind(char const* key, std::vector<geode::Keybind> const& value);

    // Stage + Dev refresh (skipped while ConfigBundle isApplying).
    static void refreshCaches();

private:
    EbDesktopMenuBind() = delete;
};

#endif
