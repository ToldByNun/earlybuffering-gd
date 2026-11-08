#ifndef DEV_OBJECTIVE_HPP
#define DEV_OBJECTIVE_HPP

#include "dev_state_diff.hpp"
#include <optional>
#include <string>
#include <vector>

// Pure objective functions over eval samples (Vertrag A). No PlayLayer.
class DevObjective {
public:
    static constexpr int kSchema = 1;

    enum class Kind {
        SurviveThenEndX = 0, // survive_then_endx_v1
        PlannerMirror,       // planner_mirror_v1
        HorizonClear         // horizon_clear_v1
    };

    class Sample {
    public:
        int delay = 0;
        bool survivedPrimary = false;
        int deathTick = -1;
        float endX = 0.f;
        bool unknown = false;
        bool budgetHit = false;
        bool aborted = false;
        bool tailFailed = false;
        int tailDeathTick = -1;
        float tailEndX = 0.f;
        bool tailComplete = false;
        bool threatCleared = false;
        int ticksRan = 0;

        bool inconclusive() const {
            return this->unknown || this->budgetHit || this->aborted;
        }

    private:
    };

    class Meta {
    public:
        DevFidelity fidelity = DevFidelity::NotReproducible;
        bool claimsAllowed = false;
        bool clockOk = true;
        bool gridFull = false;      // every d in 0..limit present, none skipped
        bool tailsComplete = false; // every primary survivor has tailComplete
        int horizonTicks = 48;
        int tailTicks = 48;
        float latencyWeight = 0.25f; // planner_mirror_v1
        bool objectivePartial = false;

    private:
    };

    class Result {
    public:
        Kind kind = Kind::SurviveThenEndX;
        char const* objectiveId = "survive_then_endx_v1";
        int horizonTicks = 48;
        int tailTicks = 48;
        bool objectivePartial = false;

        bool hasBestSurviving = false;
        int bestSurvivingDelay = -1;

        bool hasBestEvaluated = false;
        int bestEvaluatedDelay = -1;

        bool idealEligible = false;
        bool hasIdealDelay = false;
        int idealDelay = -1;
        std::string idealBlockedReason;

    private:
    };

    static char const* kindId(Kind offset);
    static bool parseKind(std::string const& s, Kind& out);

    // bestSurvivingDelay: among survivedPrimary && !inconclusive; max endX, then min delay.
    static std::optional<int> bestSurvivingDelay(std::vector<Sample> const& samples);

    static Result evaluateSurviveThenEndX(std::vector<Sample> const& samples, Meta const& meta);
    static Result evaluatePlannerMirror(std::vector<Sample> const& samples, Meta const& meta);
    static Result evaluateHorizonClear(std::vector<Sample> const& samples, Meta const& meta);

    static Result evaluate(Kind kind, std::vector<Sample> const& samples, Meta const& meta);

    // Classify primary/tail from a combined H+T schedule run.
    // deathTick/ticksRan are schedule indices (0-based); action at `delay`.
    static void classifyPrimaryAndTail(Sample& s, int delay, int horizonTicks, int tailTicks,
                                       bool died, int deathTick, float endX, bool unknown,
                                       bool budgetHit, bool aborted, int ticksRan);

    static bool selfCheck(std::string& report);

private:
    DevObjective() = delete;

    static void applyClaimsGate(Result& r, Meta const& meta);
    static void fillIdealGate(Result& r, Meta const& meta, Kind kind, bool clearSetNonEmpty);
    static bool clearEligible(Sample const& s);
};

#endif
