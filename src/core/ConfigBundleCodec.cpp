#include "ConfigBundleCodec.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

void ConfigBundleCodec::trimInPlace(std::string& s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
}

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

std::string ConfigBundleCodec::keybindsToString(std::vector<geode::Keybind> const& binds) {
    if (binds.empty()) {
        return std::string();
    }
    std::string out = binds.front().toString();
    for (size_t i = 1; i < binds.size(); ++i) {
        out += " | ";
        out += binds[i].toString();
    }
    return out;
}

bool ConfigBundleCodec::stringToKeybinds(std::string const& text, std::vector<geode::Keybind>& out) {
    out.clear();
    std::string const& src = text;
    if (src.empty()) {
        return false;
    }

    size_t start = 0;
    while (start <= src.size()) {
        size_t const sep = src.find(" | ", start);
        std::string piece = (sep == std::string::npos)
            ? src.substr(start)
            : src.substr(start, sep - start);
        ConfigBundleCodec::trimInPlace(piece);
        if (!piece.empty()) {
            auto parsed = geode::Keybind::fromString(piece);
            if (parsed.isErr()) {
                out.clear();
                return false;
            }
            out.push_back(parsed.unwrap());
        }
        if (sep == std::string::npos) {
            break;
        }
        start = sep + 3;
    }

    return !out.empty();
}

bool ConfigBundleCodec::jsonToKeybinds(matjson::Value const& value, std::vector<geode::Keybind>& out) {
    out.clear();

    // Portable string form: "F6" or "Ctrl+Shift+D | Alt+F6"
    auto asStr = value.asString();
    if (asStr.isOk()) {
        return ConfigBundleCodec::stringToKeybinds(std::string(asStr.unwrap()), out);
    }

    // JSON array of strings (optional multi-bind form).
    if (value.isArray()) {
        auto const& arr = value.asArray().unwrap();
        for (auto const& item : arr) {
            auto piece = item.asString();
            if (piece.isErr()) {
                out.clear();
                return false;
            }
            std::string s(piece.unwrap());
            ConfigBundleCodec::trimInPlace(s);
            if (s.empty()) {
                continue;
            }
            auto parsed = geode::Keybind::fromString(s);
            if (parsed.isErr()) {
                out.clear();
                return false;
            }
            out.push_back(parsed.unwrap());
        }
        return !out.empty();
    }

    return false;
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

    // Keybind string round-trip (Geode Keybind::fromString / toString).
    {
        auto one = geode::Keybind::fromString("F6");
        expect(one.isOk(), "kb-parse-f6");
        if (one.isOk()) {
            std::vector<geode::Keybind> binds{ one.unwrap() };
            std::string const s = ConfigBundleCodec::keybindsToString(binds);
            expect(!s.empty(), "kb-to-string-nonempty");
            std::vector<geode::Keybind> backBinds;
            expect(ConfigBundleCodec::stringToKeybinds(s, backBinds), "kb-from-string");
            expect(backBinds.size() == 1 && backBinds[0] == binds[0], "kb-roundtrip");
        }

        auto combo = geode::Keybind::fromString("Ctrl+Shift+D");
        expect(combo.isOk(), "kb-parse-combo");
        if (combo.isOk() && one.isOk()) {
            geode::Keybind const kbA = one.unwrap();
            geode::Keybind const kbB = combo.unwrap();
            std::vector<geode::Keybind> multi{ kbA, kbB };
            std::string const joined = ConfigBundleCodec::keybindsToString(multi);
            expect(joined.find(" | ") != std::string::npos, "kb-multi-sep");
            std::vector<geode::Keybind> backMulti;
            expect(ConfigBundleCodec::stringToKeybinds(joined, backMulti), "kb-multi-parse");
            expect(backMulti.size() == 2, "kb-multi-count");
        }

        std::vector<geode::Keybind> emptyOut;
        expect(!ConfigBundleCodec::stringToKeybinds("", emptyOut), "kb-empty-fail");
        std::vector<geode::Keybind> bad;
        expect(!ConfigBundleCodec::stringToKeybinds("NotARealKey!!!", bad), "kb-bad-fail");

        auto arrParsed = matjson::parse(R"(["F6","Ctrl+Shift+D"])");
        if (arrParsed.isOk()) {
            std::vector<geode::Keybind> fromArr;
            expect(ConfigBundleCodec::jsonToKeybinds(arrParsed.unwrap(), fromArr), "kb-json-array");
            expect(fromArr.size() == 2, "kb-json-array-count");
        }
        else {
            expect(false, "kb-json-array-parse");
        }

        auto strParsed = matjson::parse(R"("Ctrl+Shift+M")");
        if (strParsed.isOk()) {
            std::vector<geode::Keybind> fromStr;
            expect(ConfigBundleCodec::jsonToKeybinds(strParsed.unwrap(), fromStr), "kb-json-string");
            expect(fromStr.size() == 1, "kb-json-string-count");
        }
        else {
            expect(false, "kb-json-string-parse");
        }
    }

    if (fails == 0) {
        report = "ConfigBundleCodec selfCheck OK";
        return true;
    }
    report = std::string("ConfigBundleCodec selfCheck fails: ") + report;
    return false;
}
