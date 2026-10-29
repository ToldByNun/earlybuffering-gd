#ifndef DEV_CONFIG_HPP
#define DEV_CONFIG_HPP

#include <string>

// Cached Dev Lab settings (mod.json keys under title-dev* / dev-*).
// Safe defaults: recording off. refreshCache from StageConfig watchers / load.
class DevConfig {
public:
    enum class EvalGrid {
        Full = 0,
        Band,
        Sparse
    };

    enum class Objective {
        SurviveThenEndX = 0, // survive_then_endx_v1
        PlannerMirror,       // planner_mirror_v1
        HorizonClear         // horizon_clear_v1
    };

    static void refreshCache();

    // --- Session ---
    static bool recordEnable();
    static int recordCount(); // clamped 1..50
    static bool recordIncludePass();
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
    static char const* objectiveId(Objective obj);
    static bool evalRequireDeps();
    static bool recordCands();
    static int evalBudgetUs();
    static int evalTickBudget();

    // --- Overlay ---
    static bool overlay();

    static bool selfCheck(std::string& report);

private:
    DevConfig() = delete;
};

#endif // DEV_CONFIG_HPP
