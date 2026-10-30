#ifndef EARLYBUFFER_CORE_CONFIGBUNDLECODEC_HPP
#define EARLYBUFFER_CORE_CONFIGBUNDLECODEC_HPP

#include <string>
#include <vector>
#include <Geode/Geode.hpp>

// Encode/decode helpers for ConfigBundle JSON (color hex + keybind strings).
class ConfigBundleCodec {
public:
    // "#RRGGBB" (uppercase hex).
    static std::string colorToHex(cocos2d::ccColor3B color);

    // Accepts "#RRGGBB" / "#rrggbb" (optional leading '#'). false on bad length/digits.
    static bool hexToColor(std::string const& hex, cocos2d::ccColor3B& out);

    // Geode keybind settings are vector<Keybind>. Portable JSON uses a string
    // (one bind) or "bind1 | bind2" for multiples — same human form as mod.json defaults.
    static std::string keybindsToString(std::vector<geode::Keybind> const& binds);
    static bool stringToKeybinds(std::string const& text, std::vector<geode::Keybind>& out);

    // Import helper: string, or JSON array of strings. false → leave setting unchanged.
    static bool jsonToKeybinds(matjson::Value const& value, std::vector<geode::Keybind>& out);

    static bool selfCheck(std::string& report);

private:
    ConfigBundleCodec() = default;

    static int hexNibble(char c);
    static void trimInPlace(std::string& s);
};

#endif // EARLYBUFFER_CORE_CONFIGBUNDLECODEC_HPP
