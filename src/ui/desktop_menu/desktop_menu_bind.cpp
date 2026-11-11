#include "desktop_menu_bind.hpp"
#include "../../config/stage_config.hpp"
#include "../../core/bundle/config_bundle_codec.hpp"
#include "../../dev/dev_config.hpp"

class EbDesktopMenuBindHelpers {
public:
    static geode::prelude::Mod* modOrNull() {
        return geode::prelude::Mod::get();
    }

    static bool keyOk(char const* key) {
        return key != nullptr && key[0] != '\0';
    }

    static bool settingPresent(geode::prelude::Mod* mod, char const* key) {
        if (!mod || !keyOk(key)) {
            return false;
        }
        return static_cast<bool>(mod->getSetting(key));
    }

    static void afterWrite() {
        EbDesktopMenuBind::refreshCaches();
    }

    static bool colorsEqual(cocos2d::ccColor3B const& left, cocos2d::ccColor3B const& right) {
        return left.r == right.r && left.g == right.g && left.b == right.b;
    }

    static bool keybindsEqual(std::vector<geode::Keybind> const& left,
                              std::vector<geode::Keybind> const& right) {
        return EbConfigBundleCodec::keybindsToString(left) ==
               EbConfigBundleCodec::keybindsToString(right);
    }

private:
    EbDesktopMenuBindHelpers() = delete;
};

bool EbDesktopMenuBind::exists(char const* key) {
    return EbDesktopMenuBindHelpers::settingPresent(EbDesktopMenuBindHelpers::modOrNull(), key);
}

bool EbDesktopMenuBind::getBool(char const* key, bool fallback) {
    auto* mod = EbDesktopMenuBindHelpers::modOrNull();
    if (!EbDesktopMenuBindHelpers::settingPresent(mod, key)) {
        return fallback;
    }
    try {
        return mod->getSettingValue<bool>(key);
    } catch (...) {
        return fallback;
    }
}

bool EbDesktopMenuBind::setBool(char const* key, bool value) {
    auto* mod = EbDesktopMenuBindHelpers::modOrNull();
    if (!EbDesktopMenuBindHelpers::settingPresent(mod, key)) {
        return false;
    }
    bool current = false;
    try {
        current = mod->getSettingValue<bool>(key);
    } catch (...) {
        return false;
    }
    if (current == value) {
        return false;
    }
    mod->setSettingValue<bool>(key, value);
    EbDesktopMenuBindHelpers::afterWrite();
    return true;
}

int64_t EbDesktopMenuBind::getInt(char const* key, int64_t fallback) {
    auto* mod = EbDesktopMenuBindHelpers::modOrNull();
    if (!EbDesktopMenuBindHelpers::settingPresent(mod, key)) {
        return fallback;
    }
    try {
        return mod->getSettingValue<int64_t>(key);
    } catch (...) {
        return fallback;
    }
}

bool EbDesktopMenuBind::setInt(char const* key, int64_t value) {
    auto* mod = EbDesktopMenuBindHelpers::modOrNull();
    if (!EbDesktopMenuBindHelpers::settingPresent(mod, key)) {
        return false;
    }
    int64_t current = 0;
    try {
        current = mod->getSettingValue<int64_t>(key);
    } catch (...) {
        return false;
    }
    if (current == value) {
        return false;
    }
    mod->setSettingValue<int64_t>(key, value);
    EbDesktopMenuBindHelpers::afterWrite();
    return true;
}

double EbDesktopMenuBind::getFloat(char const* key, double fallback) {
    auto* mod = EbDesktopMenuBindHelpers::modOrNull();
    if (!EbDesktopMenuBindHelpers::settingPresent(mod, key)) {
        return fallback;
    }
    try {
        return mod->getSettingValue<double>(key);
    } catch (...) {
        return fallback;
    }
}

bool EbDesktopMenuBind::setFloat(char const* key, double value) {
    auto* mod = EbDesktopMenuBindHelpers::modOrNull();
    if (!EbDesktopMenuBindHelpers::settingPresent(mod, key)) {
        return false;
    }
    double current = 0.0;
    try {
        current = mod->getSettingValue<double>(key);
    } catch (...) {
        return false;
    }
    if (current == value) {
        return false;
    }
    mod->setSettingValue<double>(key, value);
    EbDesktopMenuBindHelpers::afterWrite();
    return true;
}

std::string EbDesktopMenuBind::getString(char const* key, std::string const& fallback) {
    auto* mod = EbDesktopMenuBindHelpers::modOrNull();
    if (!EbDesktopMenuBindHelpers::settingPresent(mod, key)) {
        return fallback;
    }
    try {
        return mod->getSettingValue<std::string>(key);
    } catch (...) {
        return fallback;
    }
}

bool EbDesktopMenuBind::setString(char const* key, std::string const& value) {
    auto* mod = EbDesktopMenuBindHelpers::modOrNull();
    if (!EbDesktopMenuBindHelpers::settingPresent(mod, key)) {
        return false;
    }
    std::string current;
    try {
        current = mod->getSettingValue<std::string>(key);
    } catch (...) {
        return false;
    }
    if (current == value) {
        return false;
    }
    mod->setSettingValue<std::string>(key, value);
    EbDesktopMenuBindHelpers::afterWrite();
    return true;
}

cocos2d::ccColor3B EbDesktopMenuBind::getColor(char const* key, cocos2d::ccColor3B fallback) {
    auto* mod = EbDesktopMenuBindHelpers::modOrNull();
    if (!EbDesktopMenuBindHelpers::settingPresent(mod, key)) {
        return fallback;
    }
    try {
        return mod->getSettingValue<cocos2d::ccColor3B>(key);
    } catch (...) {
        return fallback;
    }
}

bool EbDesktopMenuBind::setColor(char const* key, cocos2d::ccColor3B value) {
    auto* mod = EbDesktopMenuBindHelpers::modOrNull();
    if (!EbDesktopMenuBindHelpers::settingPresent(mod, key)) {
        return false;
    }
    cocos2d::ccColor3B current{};
    try {
        current = mod->getSettingValue<cocos2d::ccColor3B>(key);
    } catch (...) {
        return false;
    }
    if (EbDesktopMenuBindHelpers::colorsEqual(current, value)) {
        return false;
    }
    mod->setSettingValue<cocos2d::ccColor3B>(key, value);
    EbDesktopMenuBindHelpers::afterWrite();
    return true;
}

std::vector<geode::Keybind> EbDesktopMenuBind::getKeybind(char const* key) {
    auto* mod = EbDesktopMenuBindHelpers::modOrNull();
    if (!EbDesktopMenuBindHelpers::settingPresent(mod, key)) {
        return {};
    }
    try {
        return mod->getSettingValue<std::vector<geode::Keybind>>(key);
    } catch (...) {
        return {};
    }
}

bool EbDesktopMenuBind::setKeybind(char const* key, std::vector<geode::Keybind> const& value) {
    auto* mod = EbDesktopMenuBindHelpers::modOrNull();
    if (!EbDesktopMenuBindHelpers::settingPresent(mod, key)) {
        return false;
    }
    std::vector<geode::Keybind> current;
    try {
        current = mod->getSettingValue<std::vector<geode::Keybind>>(key);
    } catch (...) {
        return false;
    }
    if (EbDesktopMenuBindHelpers::keybindsEqual(current, value)) {
        return false;
    }
    mod->setSettingValue<std::vector<geode::Keybind>>(key, value);
    EbDesktopMenuBindHelpers::afterWrite();
    return true;
}

void EbDesktopMenuBind::refreshCaches() {
    if (EbStageConfig::isApplying()) {
        return;
    }
    EbStageConfig::refreshCache();
    EbDevConfig::refreshCache();
}
