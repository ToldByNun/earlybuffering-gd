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

    static geode::Result<> exportTo(std::filesystem::path const& path);
    static geode::Result<> importFrom(std::filesystem::path const& path);

    // Filled by the last successful importFrom / exportTo (zeros if never run).
    static ImportStats lastImportStats();
    static ExportStats lastExportStats();

    // Temp-file export→import + preset/look-ahead-cap guard. Restores prior values.
    static bool selfCheck(std::string& report);

private:
    ConfigBundle() = default;
};

#endif // EARLYBUFFER_CORE_CONFIGBUNDLE_HPP
