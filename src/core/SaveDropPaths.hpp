#ifndef EARLYBUFFER_CORE_SAVEDROPPATHS_HPP
#define EARLYBUFFER_CORE_SAVEDROPPATHS_HPP

#include <filesystem>
#include <string>
#include <vector>

// Mobile / non-Win I/O: fixed paths under the mod save dir (no file dialogs).
class SaveDropPaths {
public:
    static SaveDropPaths& get();

    // Mod::get()->getSaveDir(), or "earlybuffer-save" if Mod is unavailable.
    std::filesystem::path rootDir() const;
    // Drop folder for replay macros (.gdr / .gdr2 / .mhr / .json / .slc).
    std::filesystem::path replayDropDir() const;
    // Config bundle JSON (same filename as the previous non-Win fallback).
    std::filesystem::path configPath() const;
    // Dev Lab copy-report dual-write target.
    std::filesystem::path reportPath() const;

    // Creates root + replay-drop. false + err on failure.
    bool ensureDirs(std::string& err) const;

    // Replay candidates in replay-drop, newest mtime first. Missing dir → empty.
    std::vector<std::filesystem::path> listReplayCandidates() const;

    // Path-composition invariants (no required disk I/O).
    static bool selfCheck(std::string& report);

private:
    SaveDropPaths() = default;

    static bool isReplayExtension(std::filesystem::path const& path);
};

#endif // EARLYBUFFER_CORE_SAVEDROPPATHS_HPP
