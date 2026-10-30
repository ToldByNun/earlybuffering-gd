#include "ConfigBundleCodec.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

std::string ConfigBundleCodec::colorToHex(cocos2d::ccColor3B color) {
    char buf[8];
    std::snprintf(
        buf,
        sizeof(buf),
        "#%02X%02X%02X",
        static_cast<unsigned>(color.r),
        static_cast<unsigned>(color.g),
        static_cast<unsigned>(color.b)
    );
    return std::string(buf);
}

int ConfigBundleCodec::hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

bool ConfigBundleCodec::hexToColor(std::string const& hex, cocos2d::ccColor3B& out) {
    std::string_view s = hex;
    if (!s.empty() && s.front() == '#') {
        s.remove_prefix(1);
    }
    if (s.size() != 6) {
        return false;
    }
    int nibbles[6];
    for (int i = 0; i < 6; ++i) {
        nibbles[i] = ConfigBundleCodec::hexNibble(s[static_cast<size_t>(i)]);
        if (nibbles[i] < 0) {
            return false;
        }
    }
    out.r = static_cast<uint8_t>((nibbles[0] << 4) | nibbles[1]);
    out.g = static_cast<uint8_t>((nibbles[2] << 4) | nibbles[3]);
    out.b = static_cast<uint8_t>((nibbles[4] << 4) | nibbles[5]);
    return true;
}

bool ConfigBundleCodec::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    cocos2d::ccColor3B const sample{ 0x7C, 0xFF, 0x4A };
    std::string const hex = ConfigBundleCodec::colorToHex(sample);
    expect(hex == "#7CFF4A", "color-to-hex");

    cocos2d::ccColor3B back{};
    expect(ConfigBundleCodec::hexToColor(hex, back), "hex-to-color");
    expect(back.r == sample.r && back.g == sample.g && back.b == sample.b, "roundtrip");

    cocos2d::ccColor3B lower{};
    expect(ConfigBundleCodec::hexToColor("#7cff4a", lower), "hex-lower");
    expect(lower.r == sample.r && lower.g == sample.g && lower.b == sample.b, "hex-lower-rgb");

    cocos2d::ccColor3B noHash{};
    expect(ConfigBundleCodec::hexToColor("7CFF4A", noHash), "hex-no-hash");
    expect(noHash.r == sample.r, "hex-no-hash-r");

    cocos2d::ccColor3B junk{};
    expect(!ConfigBundleCodec::hexToColor("#GG0000", junk), "hex-bad-digit");
    expect(!ConfigBundleCodec::hexToColor("#FFF", junk), "hex-short");
    expect(!ConfigBundleCodec::hexToColor("", junk), "hex-empty");

    if (fails == 0) {
        report = "ConfigBundleCodec selfCheck OK";
        return true;
    }
    report = std::string("ConfigBundleCodec selfCheck fails: ") + report;
    return false;
}
