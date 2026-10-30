#include "ConfigBundle.hpp"
#include "ConfigBundleCodec.hpp"
#include "ConfigBundleKeys.hpp"
#include "../config/StageConfig.hpp"

#include <fstream>
#include <string>
#include <vector>

using namespace geode::prelude;

class ConfigBundleImportState {
public:
    static ConfigBundle::ImportStats& stats() {
        static ConfigBundle::ImportStats s;
        return s;
    }

private:
    ConfigBundleImportState() = default;
};

ConfigBundle::ImportStats ConfigBundle::lastImportStats() {
    return ConfigBundleImportState::stats();
}

geode::Result<> ConfigBundle::exportTo(std::filesystem::path const& path) {
    auto* mod = Mod::get();
    if (!mod) {
        return Err("export: mod unavailable");
    }

    matjson::Value settings = matjson::Value::object();

    for (auto const* it = ConfigBundleKeys::begin(); it != ConfigBundleKeys::end(); ++it) {
        auto const& entry = *it;
        try {
            switch (entry.kind) {
                case ConfigBundleKeys::Kind::Bool:
                    settings[entry.key] = mod->getSettingValue<bool>(entry.key);
                    break;
                case ConfigBundleKeys::Kind::Int:
                    settings[entry.key] = mod->getSettingValue<int64_t>(entry.key);
                    break;
                case ConfigBundleKeys::Kind::Float:
                    settings[entry.key] = mod->getSettingValue<double>(entry.key);
                    break;
                case ConfigBundleKeys::Kind::String:
                    settings[entry.key] = mod->getSettingValue<std::string>(entry.key);
                    break;
                case ConfigBundleKeys::Kind::Color: {
                    auto const color = mod->getSettingValue<cocos2d::ccColor3B>(entry.key);
                    settings[entry.key] = ConfigBundleCodec::colorToHex(color);
                    break;
                }
                case ConfigBundleKeys::Kind::Keybind: {
                    auto const binds = mod->getSettingValue<std::vector<geode::Keybind>>(entry.key);
                    settings[entry.key] = ConfigBundleCodec::keybindsToString(binds);
                    break;
                }
                default:
                    break;
            }
        }
        catch (...) {
            // Skip one bad key; continue exporting the rest.
        }
    }

    matjson::Value root;
    root["version"] = ConfigBundle::kFormatVersion;
    root["settings"] = settings;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return Err("export open failed");
    }
    out << root.dump();
    if (!out) {
        return Err("export write failed");
    }
    return Ok();
}

class ConfigBundleApplyGuard {
public:
    ConfigBundleApplyGuard() {
        StageConfig::beginApplying();
        this->m_active = true;
    }

    ~ConfigBundleApplyGuard() {
        this->release();
    }

    ConfigBundleApplyGuard(ConfigBundleApplyGuard const&) = delete;
    ConfigBundleApplyGuard& operator=(ConfigBundleApplyGuard const&) = delete;

    void release() {
        if (this->m_active) {
            StageConfig::endApplying();
            this->m_active = false;
        }
    }

private:
    bool m_active = false;
};

static bool applyOneSetting(
    Mod* mod,
    ConfigBundleKeys::Entry const& entry,
    matjson::Value const& value,
    int& applied,
    int& skipped
) {
    try {
        switch (entry.kind) {
            case ConfigBundleKeys::Kind::Bool: {
                auto const parsed = value.asBool();
                if (parsed.isErr()) {
                    ++skipped;
                    return false;
                }
                mod->setSettingValue<bool>(entry.key, parsed.unwrap());
                ++applied;
                return true;
            }
            case ConfigBundleKeys::Kind::Int: {
                auto const parsed = value.asInt();
                if (parsed.isErr()) {
                    ++skipped;
                    return false;
                }
                mod->setSettingValue<int64_t>(entry.key, parsed.unwrap());
                ++applied;
                return true;
            }
            case ConfigBundleKeys::Kind::Float: {
                auto const parsed = value.asDouble();
                if (parsed.isErr()) {
                    ++skipped;
                    return false;
                }
                mod->setSettingValue<double>(entry.key, parsed.unwrap());
                ++applied;
                return true;
            }
            case ConfigBundleKeys::Kind::String: {
                auto const parsed = value.asString();
                if (parsed.isErr()) {
                    ++skipped;
                    return false;
                }
                mod->setSettingValue<std::string>(entry.key, std::string(parsed.unwrap()));
                ++applied;
                return true;
            }
            case ConfigBundleKeys::Kind::Color: {
                auto const parsed = value.asString();
                if (parsed.isErr()) {
                    ++skipped;
                    return false;
                }
                cocos2d::ccColor3B color{};
                if (!ConfigBundleCodec::hexToColor(std::string(parsed.unwrap()), color)) {
                    ++skipped;
                    return false;
                }
                mod->setSettingValue<cocos2d::ccColor3B>(entry.key, color);
                ++applied;
                return true;
            }
            case ConfigBundleKeys::Kind::Keybind: {
                std::vector<geode::Keybind> binds;
                if (!ConfigBundleCodec::jsonToKeybinds(value, binds)) {
                    ++skipped;
                    return false;
                }
                mod->setSettingValue<std::vector<geode::Keybind>>(entry.key, binds);
                ++applied;
                return true;
            }
            default:
                ++skipped;
                return false;
        }
    }
    catch (...) {
        ++skipped;
        return false;
    }
}

geode::Result<> ConfigBundle::importFrom(std::filesystem::path const& path) {
    auto& stats = ConfigBundleImportState::stats();
    stats = {};

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Err("import open failed");
    }
    std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (body.empty()) {
        return Err("import empty");
    }

    auto parsed = matjson::parse(body);
    if (parsed.isErr()) {
        return Err("import json");
    }
    matjson::Value root = parsed.unwrap();
    if (!root.isObject()) {
        return Err("import root not object");
    }

    auto* mod = Mod::get();
    if (!mod) {
        return Err("import: mod unavailable");
    }

    // Resolve settings object: v2 nested, or v1 flat root.
    matjson::Value settings = matjson::Value::object();
    int version = 1;
    if (root.contains("version")) {
        auto const ver = root["version"].asInt();
        if (ver.isOk()) {
            version = static_cast<int>(ver.unwrap());
        }
    }

    if (root.contains("settings") && root["settings"].isObject()) {
        settings = root["settings"];
        if (version < 2) {
            version = 2; // nested settings imply v2 shape even if version missing/wrong
        }
    }
    else {
        // v1 flat: copy only known portable keys from root (ignore unknown / "version").
        version = 1;
        for (auto const* it = ConfigBundleKeys::begin(); it != ConfigBundleKeys::end(); ++it) {
            if (root.contains(it->key)) {
                settings[it->key] = root[it->key];
            }
        }
    }

    stats.version = version;
    int applied = 0;
    int skipped = 0;

    {
        ConfigBundleApplyGuard guard;

        for (auto const* it = ConfigBundleKeys::begin(); it != ConfigBundleKeys::end(); ++it) {
            if (!settings.contains(it->key)) {
                continue; // merge: leave live value
            }
            applyOneSetting(mod, *it, settings[it->key], applied, skipped);
        }

        // Preset wins over a mismatched stage-index from the file.
        if (settings.contains("preset")) {
            auto const presetParsed = settings["preset"].asString();
            if (presetParsed.isOk()) {
                std::string const presetName(presetParsed.unwrap());
                int const idx = StageConfig::presetIndexByName(presetName);
                try {
                    mod->setSettingValue<int64_t>("stage-index", idx);
                    mod->setSettingValue<std::string>("preset", presetName);
                }
                catch (...) {
                    ++skipped;
                }
                StageConfig::notePresetSynced(presetName);
            }
        }

        guard.release();
    }

    StageConfig::refreshCache();

    stats.applied = applied;
    stats.skipped = skipped;
    return Ok();
}
