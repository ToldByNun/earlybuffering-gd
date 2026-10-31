#ifndef EARLYBUFFER_DEV_DEVDIAGDUMP_HPP
#define EARLYBUFFER_DEV_DEVDIAGDUMP_HPP

#include "../core/ReplayImport.hpp"

#include <filesystem>
#include <string>
#include <vector>

// Writes a timestamped folder of diagnostic text logs under mod save dir /diag/.
// Covers settings, clock, replay, BakeMap, Trigger World, bake probe, self-checks.
class DevDiagDump {
public:
    // Full dump (Dev tab). Returns folder path; errMsg on failure.
    static bool dumpAll(std::filesystem::path& outDir, std::string& errMsg);

    // Compact bake-fail dump (auto from BakeMap on 0 windows).
    static bool dumpBakeFail(std::filesystem::path& outDir, std::string& errMsg);

private:
    DevDiagDump() = default;

    static std::filesystem::path makeStampDir(char const* prefix, std::string& errMsg);
    static bool writeFile(
        std::filesystem::path const& dir,
        char const* name,
        std::string const& body,
        std::string& errMsg
    );

    static std::string buildIndex(std::filesystem::path const& dir, bool bakeFailOnly);
    static std::string buildSettings();
    static std::string buildClockAssist();
    // P1 vs shadow rects + hazard overlaps after copy from start snap (pre-step).
    static std::string buildGhostHitbox();
    static std::string buildReplay();
    static std::string buildBakeMap();
    static std::string buildTriggerWorld();
    static std::string buildBakeProbe(int maxJobs);
    // Frame-by-frame: first death path + first surviving click (if any).
    static std::string buildBakeFrameTrace();
    // Remapped event list (src frame → bake frame) for first N + around death.
    static std::string buildReplayRemap();
    static std::string buildSelfChecks();
    static std::string buildOverlay();

    // Shared: prepare remapped timeline for probe/trace (empty on error → msg in outErr).
    static bool prepareBakeEvents(
        PlayLayer*& outLayer,
        int& outSlot,
        float& outDt,
        double& outSrcTps,
        double& outBakeTps,
        std::vector<ReplayEvent>& outEv,
        std::string& outErr,
        int* outSubsteps = nullptr
    );
};

#endif
