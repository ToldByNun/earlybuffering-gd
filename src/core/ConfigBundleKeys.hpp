#ifndef EARLYBUFFER_CORE_CONFIGBUNDLEKEYS_HPP
#define EARLYBUFFER_CORE_CONFIGBUNDLEKEYS_HPP

#include <string>

// Portable Geode settings for ConfigBundle export/import.
// Excludes title rows and custom:eb-action buttons. Keep in sync with mod.json.
// Regenerate: python tools/gen_config_bundle_keys.py
class ConfigBundleKeys {
public:
    enum class Kind {
        Bool,
        Int,
        Float,
        String,
        Color,
        Keybind
    };

    struct Entry {
        char const* key;
        Kind kind;
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

#endif // EARLYBUFFER_CORE_CONFIGBUNDLEKEYS_HPP
