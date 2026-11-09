#ifndef REPLAY_IMPORT_HPP
#define REPLAY_IMPORT_HPP

class EbImportedReplayData;

#include <Geode/Geode.hpp>
#include <filesystem>
#include <string>
#include <vector>

class EbReplayEvent {
public:
    int frame = 0;
    int button = 1; // 1 = Jump
    bool player2 = false;
    bool down = true;

private:
};

class EbImportedReplay {
public:
    std::vector<EbReplayEvent> events;
    double tps = 240.0;
    std::string sourceName;
    std::string format; // "gdr2", "mhr.json", "mhr", "slc"

private:
};

class EbReplayImport {
public:
    // Detect by extension / magic and parse into a normalized replay.
    static geode::Result<EbImportedReplay> load(std::filesystem::path const& path);

    static geode::Result<EbImportedReplay> loadGdr(std::filesystem::path const& path);
    static geode::Result<EbImportedReplay> loadMhrJson(std::filesystem::path const& path);
    static geode::Result<EbImportedReplay> loadMhrBin(std::filesystem::path const& path);
    static geode::Result<EbImportedReplay> loadSlc(std::filesystem::path const& path);

private:
};

#include <algorithm>
#include <cstring>
#include <fstream>
#include <gdr/gdr.hpp>
#include <span>

class EbReplayImportImplementation {
public:
    static std::string lowerExt(std::filesystem::path const& path);
    static std::string fileStemName(std::filesystem::path const& path);
    static bool isMhrJsonName(std::filesystem::path const& path);
    static void normalizeSoloPlayer2(EbImportedReplay& out);

private:
    EbReplayImportImplementation() = delete;
};

class EbImportedReplayData : public gdr::Replay<EbImportedReplayData, gdr::Input<>> {
public:
    EbImportedReplayData() : Replay("earlybuffer", 1) {}

private:
};

#endif
