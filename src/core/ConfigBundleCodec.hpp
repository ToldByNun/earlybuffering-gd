#ifndef EARLYBUFFER_CORE_CONFIGBUNDLECODEC_HPP
#define EARLYBUFFER_CORE_CONFIGBUNDLECODEC_HPP

#include <string>
#include <cocos2d.h>

// Encode/decode helpers for ConfigBundle JSON (color hex; keybinds in a later commit).
class ConfigBundleCodec {
public:
    // "#RRGGBB" (uppercase hex).
    static std::string colorToHex(cocos2d::ccColor3B color);

    // Accepts "#RRGGBB" / "#rrggbb" (optional leading '#'). false on bad length/digits.
    static bool hexToColor(std::string const& hex, cocos2d::ccColor3B& out);

    static bool selfCheck(std::string& report);

private:
    ConfigBundleCodec() = default;

    static int hexNibble(char c);
};

#endif // EARLYBUFFER_CORE_CONFIGBUNDLECODEC_HPP
