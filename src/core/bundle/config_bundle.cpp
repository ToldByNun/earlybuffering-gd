#include "config_bundle.hpp"
#include "../../config/stage_config.hpp"
#include "../../dev/dev_config.hpp"
#include "config_bundle_codec.hpp"
#include "config_bundle_keys.hpp"
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

ConfigBundle::ImportStats ConfigBundle::lastImportStats() {
    return ConfigBundleImportState::stats();
}

ConfigBundle::ExportStats ConfigBundle::lastExportStats() {
    return ConfigBundleExportState::stats();
}

geode::Result<> ConfigBundle::exportToString(std::string& outJson) {
    auto& stats = ConfigBundleExportState::stats();
    stats = {};
    stats.version = ConfigBundle::kFormatVersion;
    outJson.clear();

    auto* mod = geode::prelude::Mod::get();
    if (!mod) {
        return geode::Err("export: mod unavailable");
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
        } catch (...) {
            // Skip one bad key; continue exporting the rest.
            ++skipped;
        }
    }

    matjson::Value root;
    root["version"] = ConfigBundle::kFormatVersion;
    root["settings"] = settings;
    outJson = root.dump();
    if (outJson.empty()) {
        return geode::Err("export dump empty");
    }

    stats.written = written;
    stats.skipped = skipped;
    return geode::Ok();
}

geode::Result<> ConfigBundle::exportTo(std::filesystem::path const& path) {
    std::string body;
    auto const built = ConfigBundle::exportToString(body);
    if (built.isErr()) {
        return built;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return geode::Err("export open failed");
    }
    output << body;
    if (!output) {
        return geode::Err("export write failed");
    }
    return geode::Ok();
}

geode::Result<> ConfigBundle::importFromString(std::string const& json) {
    return ConfigBundle::importFromString(json, ApplyOptions{});
}

geode::Result<> ConfigBundle::importFromString(std::string const& json, ApplyOptions const& opt) {
    auto& stats = ConfigBundleImportState::stats();
    stats = {};

    if (json.empty()) {
        return geode::Err("import empty");
    }

    auto parsed = matjson::parse(json);
    if (parsed.isErr()) {
        return geode::Err("import json");
    }
    matjson::Value root = parsed.unwrap();
    if (!root.isObject()) {
        return geode::Err("import root not object");
    }

    auto* mod = geode::prelude::Mod::get();
    if (!mod) {
        return geode::Err("import: mod unavailable");
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
    } else {
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
            if (ConfigBundleApplyHelpers::shouldSkip(*it, opt)) {
                ++skipped;
                continue;
            }
            ConfigBundleApplyHelpers::applyOneSetting(mod, *it, settings[it->key], applied,
                                                      skipped);
        }
        // Legacy preset / stage-index keys in old exports are ignored (removed).

        guard.release();
    }

    StageConfig::refreshCache();
    // Applying-guard skips setting watchers — refresh Dev Lab cache explicitly.
    DevConfig::refreshCache();

    stats.applied = applied;
    stats.skipped = skipped;
    return geode::Ok();
}

geode::Result<> ConfigBundle::importFrom(std::filesystem::path const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return geode::Err("import open failed");
    }
    std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return ConfigBundle::importFromString(body, ApplyOptions{});
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
    expect(ConfigBundleKeys::count() == 126, "portable-keys");
    expect(ConfigBundleApplyHelpers::isDevKey("dev-overlay"), "dev-key-yes");
    expect(!ConfigBundleApplyHelpers::isDevKey("look-ahead-cap"), "dev-key-no");
    expect(!ConfigBundleApplyHelpers::isDevKey(nullptr), "dev-key-null");

    {
        ConfigBundleKeys::Entry kb{"hotkey-debug", ConfigBundleKeys::Kind::Keybind};
        ConfigBundleKeys::Entry dev{"dev-overlay", ConfigBundleKeys::Kind::Bool};
        ConfigBundleKeys::Entry ticks{"ticks-all", ConfigBundleKeys::Kind::Int};
        ApplyOptions skipKb{true, false};
        ApplyOptions skipDev{false, true};
        ApplyOptions skipBoth{true, true};
        expect(ConfigBundleApplyHelpers::shouldSkip(kb, skipKb), "skip-kb");
        expect(!ConfigBundleApplyHelpers::shouldSkip(ticks, skipKb), "skip-kb-keep-ticks");
        expect(ConfigBundleApplyHelpers::shouldSkip(dev, skipDev), "skip-dev");
        expect(!ConfigBundleApplyHelpers::shouldSkip(ticks, skipDev), "skip-dev-keep-ticks");
        expect(ConfigBundleApplyHelpers::shouldSkip(kb, skipBoth), "skip-both-kb");
        expect(ConfigBundleApplyHelpers::shouldSkip(dev, skipBoth), "skip-both-dev");
        expect(!ConfigBundleApplyHelpers::shouldSkip(ticks, skipBoth), "skip-both-keep");
    }

    auto* mod = geode::prelude::Mod::get();
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
    } catch (...) {
        expect(false, "temp-path");
        report = std::string("ConfigBundle selfCheck fails: ") + report;
        return false;
    }

    // Snapshot values we will mutate, then always restore.
    int64_t snapCap = 0;
    int64_t snapTicks = 0;
    bool snapDevOverlay = false;
    try {
        snapCap = mod->getSettingValue<int64_t>("look-ahead-cap");
        snapTicks = mod->getSettingValue<int64_t>("ticks-all");
        snapDevOverlay = mod->getSettingValue<bool>("dev-overlay");
    } catch (...) {
        expect(false, "snapshot");
        report = std::string("ConfigBundle selfCheck fails: ") + report;
        return false;
    }

    auto restore = [&]() {
        ConfigBundleApplyGuard guard;
        try {
            mod->setSettingValue<int64_t>("look-ahead-cap", snapCap);
            mod->setSettingValue<int64_t>("ticks-all", snapTicks);
            mod->setSettingValue<bool>("dev-overlay", snapDevOverlay);
        } catch (...) {
        }
        guard.release();
        StageConfig::refreshCache();
        DevConfig::refreshCache();
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
    };

    // --- Export → mutate → import round-trip (file) ---
    {
        StageConfig::beginApplying();
        try {
            mod->setSettingValue<int64_t>("look-ahead-cap", 42);
            mod->setSettingValue<int64_t>("ticks-all", 7);
        } catch (...) {
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
            expect(es.written >= 110, "export-stats-written");
        }

        StageConfig::beginApplying();
        try {
            mod->setSettingValue<int64_t>("look-ahead-cap", 0);
            mod->setSettingValue<int64_t>("ticks-all", 1);
        } catch (...) {
        }
        StageConfig::endApplying();
        StageConfig::refreshCache();

        auto const importedConfig = ConfigBundle::importFrom(tmp);
        expect(importedConfig.isOk(), "import-roundtrip-ok");
        if (importedConfig.isOk()) {
            auto const is = ConfigBundle::lastImportStats();
            expect(is.version == 2, "import-stats-v2");
            expect(is.applied > 0, "import-stats-applied");
            try {
                expect(mod->getSettingValue<int64_t>("look-ahead-cap") == 42, "roundtrip-cap");
                expect(mod->getSettingValue<int64_t>("ticks-all") == 7, "roundtrip-ticks");
            } catch (...) {
                expect(false, "roundtrip-read");
            }
        }
    }

    // --- String export → import round-trip ---
    {
        StageConfig::beginApplying();
        try {
            mod->setSettingValue<int64_t>("look-ahead-cap", 33);
            mod->setSettingValue<int64_t>("ticks-all", 5);
        } catch (...) {
        }
        StageConfig::endApplying();
        StageConfig::refreshCache();

        std::string dumped;
        auto const exp = ConfigBundle::exportToString(dumped);
        expect(exp.isOk(), "export-string-ok");
        expect(!dumped.empty(), "export-string-body");

        StageConfig::beginApplying();
        try {
            mod->setSettingValue<int64_t>("look-ahead-cap", 0);
            mod->setSettingValue<int64_t>("ticks-all", 1);
        } catch (...) {
        }
        StageConfig::endApplying();
        StageConfig::refreshCache();

        auto const importedConfig = ConfigBundle::importFromString(dumped);
        expect(importedConfig.isOk(), "import-string-ok");
        if (importedConfig.isOk()) {
            try {
                expect(mod->getSettingValue<int64_t>("look-ahead-cap") == 33, "string-cap");
                expect(mod->getSettingValue<int64_t>("ticks-all") == 5, "string-ticks");
            } catch (...) {
                expect(false, "string-read");
            }
        }
    }

    // --- ApplyOptions: skipDevKeys leaves live Dev Lab bool unchanged ---
    {
        StageConfig::beginApplying();
        try {
            mod->setSettingValue<bool>("dev-overlay", false);
            mod->setSettingValue<int64_t>("look-ahead-cap", 10);
        } catch (...) {
        }
        StageConfig::endApplying();
        StageConfig::refreshCache();
        DevConfig::refreshCache();

        std::string const filtered = R"({
  "version": 2,
  "settings": {
    "look-ahead-cap": 91,
    "dev-overlay": true
  }
})";
        ApplyOptions opt{};
        opt.skipDevKeys = true;
        auto const importedConfig = ConfigBundle::importFromString(filtered, opt);
        expect(importedConfig.isOk(), "filter-dev-import");
        if (importedConfig.isOk()) {
            auto const is = ConfigBundle::lastImportStats();
            expect(is.skipped >= 1, "filter-dev-skipped");
            try {
                expect(mod->getSettingValue<int64_t>("look-ahead-cap") == 91, "filter-dev-cap");
                expect(mod->getSettingValue<bool>("dev-overlay") == false, "filter-dev-kept");
            } catch (...) {
                expect(false, "filter-dev-read");
            }
        }
    }

    // --- Applying-guard: import must not leave sentinel values wrong ---
    {
        std::ofstream output(tmp, std::ios::binary | std::ios::trunc);
        expect(static_cast<bool>(output), "guard-write");
        if (output) {
            output << R"({
  "version": 2,
  "settings": {
    "look-ahead-cap": 77,
    "ticks-all": 11
  }
})";
            output.close();

            auto const importedConfig = ConfigBundle::importFrom(tmp);
            expect(importedConfig.isOk(), "guard-import");
            if (importedConfig.isOk()) {
                try {
                    expect(mod->getSettingValue<int64_t>("look-ahead-cap") == 77, "guard-cap");
                    expect(mod->getSettingValue<int64_t>("ticks-all") == 11, "guard-ticks");
                } catch (...) {
                    expect(false, "guard-read");
                }
            }
        }
    }

    // --- v1 flat root still applies known keys; legacy preset key ignored ---
    {
        std::ofstream output(tmp, std::ios::binary | std::ios::trunc);
        if (output) {
            output << R"({"version":1,"look-ahead-cap":55,"ticks-all":3,"preset":"Normal"})";
            output.close();
            auto const importedConfig = ConfigBundle::importFrom(tmp);
            expect(importedConfig.isOk(), "v1-import");
            if (importedConfig.isOk()) {
                auto const is = ConfigBundle::lastImportStats();
                expect(is.version == 1, "v1-stats-version");
                try {
                    expect(mod->getSettingValue<int64_t>("look-ahead-cap") == 55, "v1-cap");
                    expect(mod->getSettingValue<int64_t>("ticks-all") == 3, "v1-ticks");
                } catch (...) {
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

auto ConfigBundleImportState::stats() -> ConfigBundle::ImportStats& {
    static ConfigBundle::ImportStats s;
    return s;
}

auto ConfigBundleExportState::stats() -> ConfigBundle::ExportStats& {
    static ConfigBundle::ExportStats s;
    return s;
}

ConfigBundleApplyGuard::ConfigBundleApplyGuard() {
    StageConfig::beginApplying();
    this->m_active = true;
}

ConfigBundleApplyGuard::~ConfigBundleApplyGuard() {
    this->release();
}

auto ConfigBundleApplyGuard::release() -> void {
    if (this->m_active) {
        StageConfig::endApplying();
        this->m_active = false;
    }
}

auto ConfigBundleApplyHelpers::isDevKey(char const* key) -> bool {
    if (!(!key)) {
        return std::strncmp(key, "dev-", 4) == 0;
    }
    return false;
}

auto ConfigBundleApplyHelpers::shouldSkip(ConfigBundleKeys::Entry const& entry,
                                          ConfigBundle::ApplyOptions const& opt) -> bool {
    if (opt.skipKeybinds && entry.kind == ConfigBundleKeys::Kind::Keybind) {
        return true;
    }
    if (opt.skipDevKeys && isDevKey(entry.key)) {
        return true;
    }
    return false;
}

auto ConfigBundleApplyHelpers::applyOneSetting(geode::prelude::Mod* mod,
                                               ConfigBundleKeys::Entry const& entry,
                                               matjson::Value const& value, int& applied,
                                               int& skipped) -> bool {
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
            std::string text(parsed.unwrap());
            if (std::string_view(entry.key) == "assist-mode" && text == "Route")
                text = "Live";
            mod->setSettingValue<std::string>(entry.key, text);
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
    } catch (...) {
        ++skipped;
        return false;
    }
}
