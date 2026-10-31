#include "ConfigBundle.hpp"
#include "ConfigBundleCodec.hpp"
#include "ConfigBundleKeys.hpp"
#include "../config/StageConfig.hpp"
#include "../dev/DevConfig.hpp"

#include <fstream>
#include <string>
#include <system_error>
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

class ConfigBundleExportState {
public:
    static ConfigBundle::ExportStats& stats() {
        static ConfigBundle::ExportStats s;
        return s;
    }

private:
    ConfigBundleExportState() = default;
};

ConfigBundle::ImportStats ConfigBundle::lastImportStats() {
    return ConfigBundleImportState::stats();
}

ConfigBundle::ExportStats ConfigBundle::lastExportStats() {
    return ConfigBundleExportState::stats();
}

geode::Result<> ConfigBundle::exportTo(std::filesystem::path const& path) {
    auto& stats = ConfigBundleExportState::stats();
    stats = {};
    stats.version = ConfigBundle::kFormatVersion;

    auto* mod = Mod::get();
    if (!mod) {
        return Err("export: mod unavailable");
    }

    matjson::Value settings = matjson::Value::object();
    int written = 0;
    int skipped = 0;

    for (auto const* it = ConfigBundleKeys::begin(); it != ConfigBundleKeys::end(); ++it) {
        auto const& entry = *it;
        try {
            switch (entry.kind) {
                case ConfigBundleKeys::Kind::Bool:
                    settings[entry.key] = mod->getSettingValue<bool>(entry.key);
                    ++written;
                    break;
                case ConfigBundleKeys::Kind::Int:
                    settings[entry.key] = mod->getSettingValue<int64_t>(entry.key);
                    ++written;
                    break;
                case ConfigBundleKeys::Kind::Float:
                    settings[entry.key] = mod->getSettingValue<double>(entry.key);
                    ++written;
                    break;
                case ConfigBundleKeys::Kind::String:
                    settings[entry.key] = mod->getSettingValue<std::string>(entry.key);
                    ++written;
                    break;
                case ConfigBundleKeys::Kind::Color: {
                    auto const color = mod->getSettingValue<cocos2d::ccColor3B>(entry.key);
                    settings[entry.key] = ConfigBundleCodec::colorToHex(color);
                    ++written;
                    break;
                }
                case ConfigBundleKeys::Kind::Keybind: {
                    auto const binds = mod->getSettingValue<std::vector<geode::Keybind>>(entry.key);
                    settings[entry.key] = ConfigBundleCodec::keybindsToString(binds);
                    ++written;
                    break;
                }
                default:
                    ++skipped;
                    break;
            }
        }
        catch (...) {
            // Skip one bad key; continue exporting the rest.
            ++skipped;
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

    stats.written = written;
    stats.skipped = skipped;
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
    // Applying-guard skips setting watchers — refresh Dev Lab cache explicitly.
    DevConfig::refreshCache();

    stats.applied = applied;
    stats.skipped = skipped;
    return Ok();
}

bool ConfigBundle::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(ConfigBundle::kFormatVersion == 2, "format-v2");
    expect(ConfigBundleKeys::count() == 126, "keys-126");

    auto* mod = Mod::get();
    if (!mod) {
        if (fails == 0) {
            report = "ConfigBundle selfCheck OK (no mod — structural only)";
            return true;
        }
        report = std::string("ConfigBundle selfCheck fails: ") + report;
        return false;
    }

    std::filesystem::path tmp;
    try {
        tmp = std::filesystem::temp_directory_path() / "earlybuffer-config-selfcheck.json";
    }
    catch (...) {
        expect(false, "temp-path");
        report = std::string("ConfigBundle selfCheck fails: ") + report;
        return false;
    }

    // Snapshot values we will mutate, then always restore.
    int64_t snapCap = 0;
    int64_t snapTicks = 0;
    int64_t snapStage = 0;
    std::string snapPreset;
    try {
        snapCap = mod->getSettingValue<int64_t>("look-ahead-cap");
        snapTicks = mod->getSettingValue<int64_t>("ticks-all");
        snapStage = mod->getSettingValue<int64_t>("stage-index");
        snapPreset = mod->getSettingValue<std::string>("preset");
    }
    catch (...) {
        expect(false, "snapshot");
        report = std::string("ConfigBundle selfCheck fails: ") + report;
        return false;
    }

    auto restore = [&]() {
        ConfigBundleApplyGuard guard;
        try {
            mod->setSettingValue<int64_t>("look-ahead-cap", snapCap);
            mod->setSettingValue<int64_t>("ticks-all", snapTicks);
            mod->setSettingValue<int64_t>("stage-index", snapStage);
            mod->setSettingValue<std::string>("preset", snapPreset);
            StageConfig::notePresetSynced(snapPreset);
        }
        catch (...) {
        }
        guard.release();
        StageConfig::refreshCache();
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
    };

    // --- Export → mutate → import round-trip ---
    {
        StageConfig::beginApplying();
        try {
            mod->setSettingValue<int64_t>("look-ahead-cap", 42);
            mod->setSettingValue<int64_t>("ticks-all", 7);
        }
        catch (...) {
            StageConfig::endApplying();
            restore();
            expect(false, "seed-roundtrip");
            report = std::string("ConfigBundle selfCheck fails: ") + report;
            return false;
        }
        StageConfig::endApplying();
        StageConfig::refreshCache();

        auto const exp = ConfigBundle::exportTo(tmp);
        expect(exp.isOk(), "export-ok");
        if (exp.isOk()) {
            auto const es = ConfigBundle::lastExportStats();
            expect(es.version == 2, "export-stats-v2");
            expect(es.written >= 120, "export-stats-written");
        }

        StageConfig::beginApplying();
        try {
            mod->setSettingValue<int64_t>("look-ahead-cap", 0);
            mod->setSettingValue<int64_t>("ticks-all", 1);
        }
        catch (...) {
        }
        StageConfig::endApplying();
        StageConfig::refreshCache();

        auto const imp = ConfigBundle::importFrom(tmp);
        expect(imp.isOk(), "import-roundtrip-ok");
        if (imp.isOk()) {
            auto const is = ConfigBundle::lastImportStats();
            expect(is.version == 2, "import-stats-v2");
            expect(is.applied > 0, "import-stats-applied");
            try {
                expect(mod->getSettingValue<int64_t>("look-ahead-cap") == 42, "roundtrip-cap");
                expect(mod->getSettingValue<int64_t>("ticks-all") == 7, "roundtrip-ticks");
            }
            catch (...) {
                expect(false, "roundtrip-read");
            }
        }
    }

    // --- Preset + custom look-ahead must not be wiped by applyPreset listener ---
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        expect(static_cast<bool>(out), "preset-guard-write");
        if (out) {
            // preset Tight would normally rewrite look-ahead-cap via applyPreset;
            // import must keep sentinel 77 under applying-guard.
            out << R"({
  "version": 2,
  "settings": {
    "preset": "Tight",
    "stage-index": 0,
    "look-ahead-cap": 77,
    "ticks-all": 11
  }
})";
            out.close();

            auto const imp = ConfigBundle::importFrom(tmp);
            expect(imp.isOk(), "preset-guard-import");
            if (imp.isOk()) {
                try {
                    expect(
                        mod->getSettingValue<int64_t>("look-ahead-cap") == 77,
                        "preset-guard-cap"
                    );
                    expect(
                        mod->getSettingValue<int64_t>("ticks-all") == 11,
                        "preset-guard-ticks"
                    );
                    expect(
                        mod->getSettingValue<std::string>("preset") == "Tight",
                        "preset-guard-preset"
                    );
                }
                catch (...) {
                    expect(false, "preset-guard-read");
                }
            }
        }
    }

    // --- v1 flat root still applies known keys ---
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (out) {
            out << R"({"version":1,"look-ahead-cap":55,"ticks-all":3,"preset":"Normal"})";
            out.close();
            auto const imp = ConfigBundle::importFrom(tmp);
            expect(imp.isOk(), "v1-import");
            if (imp.isOk()) {
                auto const is = ConfigBundle::lastImportStats();
                expect(is.version == 1, "v1-stats-version");
                try {
                    expect(mod->getSettingValue<int64_t>("look-ahead-cap") == 55, "v1-cap");
                    expect(mod->getSettingValue<std::string>("preset") == "Normal", "v1-preset");
                }
                catch (...) {
                    expect(false, "v1-read");
                }
            }
        }
    }

    restore();

    if (fails == 0) {
        report = "ConfigBundle selfCheck OK";
        return true;
    }
    report = std::string("ConfigBundle selfCheck fails: ") + report;
    return false;
}
