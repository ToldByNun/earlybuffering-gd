#ifndef EARLYBUFFER_CORE_CONFIGBUNDLE_HPP
#define EARLYBUFFER_CORE_CONFIGBUNDLE_HPP

#include <filesystem>
#include <string>
#include <Geode/Geode.hpp>

// Portable settings JSON. v2 = { "version": 2, "settings": { ... } }.
// Import also accepts v1 flat root (legacy keys at top level).
class ConfigBundle {
public:
    static constexpr int kFormatVersion = 2;

    struct ImportStats {
        int version = 0;
        int applied = 0;
        int skipped = 0;
    };

    struct ExportStats {
        int version = 0;
        int written = 0;
        int skipped = 0;
    };

    // Filters for cloud / partial apply. Local file import uses defaults (apply all).
    struct ApplyOptions {
        bool skipKeybinds = false;
        bool skipDevKeys = false; // keys whose name starts with "dev-"
    };

    static geode::Result<> exportTo(std::filesystem::path const& path);
    static geode::Result<> exportToString(std::string& outJson);

    static geode::Result<> importFrom(std::filesystem::path const& path);
    // Overload without options — Clang rejects default args that need nested
    // default member initializers (`opt = {}`) inside the enclosing class.
    static geode::Result<> importFromString(std::string const& json);
    static geode::Result<> importFromString(
        std::string const& json,
        ApplyOptions const& opt
    );

    // Filled by the last successful import / export (zeros if never run).
    static ImportStats lastImportStats();
    static ExportStats lastExportStats();

    // Temp-file + string round-trips, ApplyOptions filters, applying-guard.
    // Restores prior values.
    static bool selfCheck(std::string& report);

private:
    ConfigBundle() = default;
};

#endif // EARLYBUFFER_CORE_CONFIGBUNDLE_HPP
