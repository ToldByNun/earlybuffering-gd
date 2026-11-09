#ifndef DEV_CONFIG_HPP
#define DEV_CONFIG_HPP

#include <string>

// Cached Dev Lab settings (mod.json keys under title-dev* / dev-*).
// Safe defaults: recording off. refreshCache from StageConfig watchers / load.
class EbDevConfig {
public:
    enum class EvalGrid { Full = 0, Band, Sparse };

    enum class Objective {
        SurviveThenEndX = 0, // survive_then_endx_v1
        PlannerMirror,       // planner_mirror_v1
        HorizonClear         // horizon_clear_v1
    };

    static void refreshCache();

    // --- Session ---
    static bool recordEnable();
    static int recordCount(); // clamped 1..50
    static bool countEmptyAttempts();
    static bool freezeWorld();
    // When recording: kill explore / verify-AB randomness (default on).
    static bool disableRandom();

    // --- Eval ---
    static bool evalOnDeath();
    static EvalGrid evalGrid();
    static char const* evalGridId(EvalGrid grid);
    static int evalHorizon(); // primary horizon ticks; default 48
    static int evalTail();    // tail ticks after primary; default 48
    static Objective objective();
    static char const* objectiveId(Objective object);
    static bool evalRequireDeps();
    static bool recordCands();
    static int evalTickBudget();

    // --- Overlay ---
    static bool overlay();

    static bool selfCheck(std::string& report);

private:
    EbDevConfig() = delete;
};

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <algorithm>
#include <cstring>
#include <fmt/format.h>

class EbDevConfigImplementation {
public:
    static int clampInt(int value, int lo, int hi);
    static int readInt(geode::prelude::Mod* mod, char const* id, int fallback);
    static bool readBool(geode::prelude::Mod* mod, char const* id, bool fallback);
    static std::string readString(geode::prelude::Mod* mod, char const* id, char const* fallback);
    static EbDevConfig::EvalGrid parseEvalGrid(std::string const& s);
    static EbDevConfig::Objective parseObjective(std::string const& s);
    static void ensureCache();

private:
    friend class EbDevConfig;

    static bool s_devCacheInit;
    static bool s_devRecordEnable;
    static int s_devRecordCount;
    static bool s_devCountEmptyAttempts;
    static bool s_devFreezeWorld;
    static bool s_devDisableRandom;
    static bool s_devEvalOnDeath;
    static EbDevConfig::EvalGrid s_devEvalGrid;
    static int s_devEvalHorizon;
    static int s_devEvalTail;
    static EbDevConfig::Objective s_devObjective;
    static bool s_devEvalRequireDeps;
    static bool s_devRecordCands;
    static int s_devEvalTickBudget;
    static bool s_devOverlay;

    EbDevConfigImplementation() = delete;
};

#endif
