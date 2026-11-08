#ifndef DEV_DIAG_DUMP_HPP
#define DEV_DIAG_DUMP_HPP

#include "../../core/replay/replay_import.hpp"
#include "../../core/sim/shadow_simulator.hpp"
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

    // Pure helpers (PlanGold + BakeMap validate Err).
    static int deathTraceUntil(int deathFrame, int lastEventFrame);
    static std::string formatUnknownTop3(std::vector<ShadowSimulator::UnknownHistBin> const& hist);
    static bool selfCheck(std::string& report);

private:
    friend class DevStateDiagnostics;

    friend class DevBakeDiagnostics;

    friend class DevGhostDiagnostics;

    DevDiagDump() = default;

    static std::filesystem::path makeStampDir(char const* prefix, std::string& errMsg);
    static bool writeFile(std::filesystem::path const& dir, char const* name,
                          std::string const& body, std::string& errMsg);

    static std::string buildIndex(std::filesystem::path const& dir, bool bakeFailOnly);
    static std::string buildSettings();
    static std::string buildClockAssist();
    // P1 vs shadow rects + hazard overlaps after copy from start snap (pre-step).
    static std::string buildGhostHitbox();
    static std::string buildReplay();
    static std::string buildBakeMap();
    static std::string buildTriggerWorld();
    static std::string buildBakeProbe(int maxJobs);
    // Frame-by-frame through deathFrame+8 (Commit 3 — no hard 128 cap).
    static std::string buildBakeFrameTrace();
    // Soft/Hard unknown overlap histogram from a death+8 bake timeline.
    static std::string buildUnknownHistogram();
    // Remapped event list AFTER prepareBakeTimeline (incl. optional −1 shift).
    static std::string buildReplayRemap();
    static std::string buildSelfChecks();
    static std::string buildOverlay();

    class BakePrepStats {
    public:
        int droppedP2 = 0;
        bool forcedP2toP1 = false;
        bool shiftedMinus1 = false;
        int minFrameBeforeShift = -1;

    private:
    };

    // Shared: prepare remapped timeline for probe/trace (empty on error → msg in outErr).
    static bool prepareBakeEvents(PlayLayer*& outLayer, int& outSlot, float& outDt,
                                  double& outSrcTps, double& outBakeTps,
                                  std::vector<ReplayEvent>& outEv, std::string& outErr,
                                  int* outSubsteps = nullptr, BakePrepStats* outStats = nullptr);
};

#include "../../core/bundle/config_bundle_keys.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

class DevDiagDumpImplementation {
public:
    static std::string stampNow();
    static bool writeTextFile(std::filesystem::path const& path, std::string const& body);
    static char const* boolStr(bool value);
    static std::string settingValueLine(ConfigBundleKeys::Entry const& e);
    static void dumpVehicleFlags(std::ostringstream& o, char const* prefix, PlayerObject* player);
    static bool isHazardType(GameObject* object);
    static void dumpRectLine(std::ostringstream& o, char const* name, PlayerObject* player);
    static int countHazardOverlaps(PlayLayer* layer, PlayerObject* player,
                                   std::ostringstream& detail, int maxDetail);

private:
    friend class DevGhostDiagnostics;

    DevDiagDumpImplementation() = delete;
};

#endif
