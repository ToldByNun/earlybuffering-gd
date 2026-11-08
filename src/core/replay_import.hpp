#ifndef REPLAY_IMPORT_HPP
#define REPLAY_IMPORT_HPP

class ImportedReplayData;

#include <Geode/Geode.hpp>
#include <filesystem>
#include <string>
#include <vector>

class ReplayEvent {
public:
    int frame = 0;
    int button = 1; // 1 = Jump
    bool player2 = false;
    bool down = true;

private:
};

class ImportedReplay {
public:
    std::vector<ReplayEvent> events;
    double tps = 240.0;
    std::string sourceName;
    std::string format; // "gdr2", "mhr.json", "mhr", "slc"

private:
};

class ReplayImport {
public:
    // Detect by extension / magic and parse into a normalized replay.
    static geode::Result<ImportedReplay> load(std::filesystem::path const& path);

    static geode::Result<ImportedReplay> loadGdr(std::filesystem::path const& path);
    static geode::Result<ImportedReplay> loadMhrJson(std::filesystem::path const& path);
    static geode::Result<ImportedReplay> loadMhrBin(std::filesystem::path const& path);
    static geode::Result<ImportedReplay> loadSlc(std::filesystem::path const& path);

private:
};

#include <algorithm>
#include <cstring>
#include <fstream>
#include <gdr/gdr.hpp>
#include <span>

class ReplayImportImplementation {
public:
    static std::string lowerExt(std::filesystem::path const& path);
    static std::string fileStemName(std::filesystem::path const& path);
    static bool isMhrJsonName(std::filesystem::path const& path);
    static void normalizeSoloPlayer2(ImportedReplay& out);

private:
    ReplayImportImplementation() = delete;
};

class ImportedReplayData : public gdr::Replay<ImportedReplayData, gdr::Input<>> {
public:
    ImportedReplayData() : Replay("earlybuffer", 1) {}

private:
};

#endif
