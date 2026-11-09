#ifndef CONFIG_BUNDLE_CODEC_HPP
#define CONFIG_BUNDLE_CODEC_HPP

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

// Encode/decode helpers for ConfigBundle JSON (color hex + keybind strings).
class EbConfigBundleCodec {
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
    EbConfigBundleCodec() = default;

    static int hexNibble(char character);
    static void trimInPlace(std::string& s);
};

#endif
