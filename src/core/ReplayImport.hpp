#ifndef FRIRECRACKER_CORE_REPLAYIMPORT_HPP
#define FRIRECRACKER_CORE_REPLAYIMPORT_HPP

#include <Geode/Geode.hpp>
#include <string>
#include <vector>
#include <filesystem>


struct ReplayEvent {
    int frame = 0;
    int button = 1; // 1 = Jump
    bool player2 = false;
    bool down = true;
};

struct ImportedReplay {
    std::vector<ReplayEvent> events;
    double tps = 240.0;
    std::string sourceName;
    std::string format; // "gdr2", "mhr.json", "mhr", "slc"
};

class ReplayImport {
public:
    // Detect by extension / magic and parse into a normalized replay.
    static geode::Result<ImportedReplay> load(std::filesystem::path const& path);

    static geode::Result<ImportedReplay> loadGdr(std::filesystem::path const& path);
    static geode::Result<ImportedReplay> loadMhrJson(std::filesystem::path const& path);
    static geode::Result<ImportedReplay> loadMhrBin(std::filesystem::path const& path);
    static geode::Result<ImportedReplay> loadSlc(std::filesystem::path const& path);
};

#endif // FRIRECRACKER_CORE_REPLAYIMPORT_HPP
