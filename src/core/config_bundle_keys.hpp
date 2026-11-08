#ifndef CONFIG_BUNDLE_KEYS_HPP
#define CONFIG_BUNDLE_KEYS_HPP

class ConfigBundleKeysTable;

#include <string>

// Portable Geode settings for ConfigBundle export/import.
// Excludes title rows and custom:eb-action buttons. Keep in sync with mod.json.
// Regenerate: python tools/gen_config_bundle_keys.py
class ConfigBundleKeys {
public:
    enum class Kind { Bool, Int, Float, String, Color, Keybind };

    class Entry {
    public:
        char const* key;
        Kind kind;

    private:
    };

    static Entry const* begin();
    static Entry const* end();
    static int count();
    static Entry const* find(char const* key);

    // Count + uniqueness + kind validity. No Mod / disk required.
    static bool selfCheck(std::string& report);

private:
    ConfigBundleKeys() = default;
};

#include <cstring>
#include <unordered_set>

class ConfigBundleKeysTable {
public:
    static ConfigBundleKeys::Entry const kEntries[125];
    static constexpr int kEntryCount = 125;

private:
    ConfigBundleKeysTable() = default;
};

#endif
