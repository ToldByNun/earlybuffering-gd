#ifndef CONFIG_BUNDLE_HPP
#define CONFIG_BUNDLE_HPP

class ConfigBundleImportState;
class ConfigBundleExportState;
class ConfigBundleApplyGuard;
class ConfigBundleApplyHelpers;

#include <Geode/Geode.hpp>
#include <filesystem>
#include <string>

// Portable settings JSON. v2 = { "version": 2, "settings": { ... } }.
// Import also accepts v1 flat root (legacy keys at top level).
class ConfigBundle {
public:
    static constexpr int kFormatVersion = 2;

    class ImportStats {
    public:
        int version = 0;
        int applied = 0;
        int skipped = 0;

    private:
    };

    class ExportStats {
    public:
        int version = 0;
        int written = 0;
        int skipped = 0;

    private:
    };

    // Filters for cloud / partial apply. Local file import uses defaults (apply all).
    class ApplyOptions {
    public:
        bool skipKeybinds = false;
        bool skipDevKeys = false; // keys whose name starts with "dev-"

    private:
    };

    static geode::Result<> exportTo(std::filesystem::path const& path);
    static geode::Result<> exportToString(std::string& outJson);

    static geode::Result<> importFrom(std::filesystem::path const& path);
    // Overload without options — Clang rejects default args that need nested
    // default member initializers (`opt = {}`) inside the enclosing class.
    static geode::Result<> importFromString(std::string const& json);
    static geode::Result<> importFromString(std::string const& json, ApplyOptions const& opt);

    // Filled by the last successful import / export (zeros if never run).
    static ImportStats lastImportStats();
    static ExportStats lastExportStats();

    // Temp-file + string round-trips, ApplyOptions filters, applying-guard.
    // Restores prior values.
    static bool selfCheck(std::string& report);

private:
    ConfigBundle() = default;
};

#include "../config/stage_config.hpp"
#include "../dev/dev_config.hpp"
#include "config_bundle_codec.hpp"
#include "config_bundle_keys.hpp"
#include <cstring>
#include <fstream>
#include <system_error>
#include <vector>

class ConfigBundleImportState {
public:
    static ConfigBundle::ImportStats& stats();

private:
    ConfigBundleImportState() = default;
};

class ConfigBundleExportState {
public:
    static ConfigBundle::ExportStats& stats();

private:
    ConfigBundleExportState() = default;
};

class ConfigBundleApplyGuard {
public:
    ConfigBundleApplyGuard();

    ~ConfigBundleApplyGuard();

    ConfigBundleApplyGuard(ConfigBundleApplyGuard const&) = delete;
    ConfigBundleApplyGuard& operator=(ConfigBundleApplyGuard const&) = delete;

    void release();

private:
    bool m_active = false;
};

class ConfigBundleApplyHelpers {
public:
    static bool isDevKey(char const* key);

    static bool shouldSkip(ConfigBundleKeys::Entry const& entry,
                           ConfigBundle::ApplyOptions const& opt);

    static bool applyOneSetting(geode::prelude::Mod* mod, ConfigBundleKeys::Entry const& entry,
                                matjson::Value const& value, int& applied, int& skipped);

private:
    ConfigBundleApplyHelpers() = default;
};

#endif
