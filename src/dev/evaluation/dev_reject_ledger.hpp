#ifndef DEV_REJECT_LEDGER_HPP
#define DEV_REJECT_LEDGER_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Per-delay reject ledger + attribution (Commit 12 / Vertrag D).
class EbDevRejectLedger {
public:
    static constexpr int kMaxCandNotes = 64;
    static constexpr int kMaxDetailLen = 120;

    enum class Status { Chosen = 0, Evaluated, Rejected, Skipped };

    enum class RejectCode {
        Chosen = 0,
        NotViable,
        DiedBeforeAction,
        WorseSurviveBin,
        WorseEndX,
        OutsideOrbWindow,
        RobustKFailed,
        BudgetNotReached,
        PreferBandEarlyExit,
        WaveFutile,
        WaveHazardProximity,
        DualFailed,
        UnknownTrunc,
        LegacyEarlierKept,
        VerifyFailed,
        LearningDisplaced,
        SameCrashCut,
        EarliestSafeExit,
        PeakDeathCut
    };

    enum class Attribution {
        ChosenMatchesBestSurviving = 0,
        ChosenDiffersBestSurviving,
        BestSurvivingAbsent,
        ApproxOnly,
        NotReproducible,
        LearningNudged,
        LearningFloored,
        RecheckSlide,
        HusDeadline,
        VerifyRefused,
        GatePass,
        LimitExcluded,
        DualFailed,
        UnknownTrunc,
        SkippedByPlanner
    };

    class CandSnap {
    public:
        bool evaluated = false;
        bool viable = false;
        bool diedBeforeAction = false;
        bool dualFailed = false;
        bool unknownTrunc = false;
        bool clearsThreat = false;
        bool activatedRing = false;
        bool robustKFailed = false;
        bool verifyFailed = false;
        int surviveBin = 0;
        float endX = 0.f;
        // AP4: structured run truth (from candidate.raw + ModeSurvival).
        bool hasRunTruth = false;
        int deathTick = -1;
        int ticksRan = 0;
        bool unknown = false;
        bool budgetHit = false;
        int surviveTicks = 0;
        std::string survivalClass;

    private:
    };

    enum class EarlyExit {
        None = 0,
        Budget,
        PreferBand,
        WaveProximity,
        NoThreat,
        WaveFutile,
        SameCrash,
        EarliestSafe,
        PeakDeath
    };

    class BuildInput {
    public:
        int limit = 0;
        int chosenDelay = -1; // Pass→0 when doPass; −1 = no pick
        bool doPass = false;
        float latencyWeight = 0.25f;
        bool orbIntent = false;
        int actMin = -1;
        int actMax = -1;
        bool legacyPick = false;
        EarlyExit earlyExit = EarlyExit::None;
        bool includeScores = true;
        std::vector<CandSnap> snaps; // size should be limit+1

    private:
    };

    class CandNote {
    public:
        int delay = 0;
        Status status = Status::Skipped;
        RejectCode rejectCode = RejectCode::NotViable;
        std::string detail;
        bool hasScores = false;
        int surviveBin = 0;
        float endX = 0.f;
        float latencyTerm = 0.f;
        bool threatCleared = false;
        float score = 0.f;
        // AP4: structured run truth alongside scores/rejects.
        bool hasRunTruth = false;
        int deathTick = -1;
        int ticksRan = 0;
        bool unknown = false;
        bool budgetHit = false;
        int surviveTicks = 0;
        std::string survivalClass;

    private:
    };

    class Ledger {
    public:
        std::vector<CandNote> notes;
        bool truncated = false; // scores omitted past kMaxCandNotes

    private:
    };

    class AttributionInput {
    public:
        bool hasPlanFinished = false;
        bool gatePass = false;
        bool claimsAllowed = false;
        bool approx = false;
        bool notReproducible = false;
        bool hasBestSurviving = false;
        int bestSurvivingDelay = -1;
        int plannedDelay = -1;
        char const* objectiveId = "";
        bool learningNudged = false;
        bool learningFloored = false;
        bool recheckSlide = false;
        bool husDeadline = false;
        bool verifyRefused = false;
        bool limitExcluded = false;
        Ledger const* ledger = nullptr;

    private:
    };

    class AttributionNote {
    public:
        Attribution code = Attribution::GatePass;
        std::string detail;

    private:
    };

    static char const* statusId(Status state);
    static bool parseStatus(std::string const& s, Status& out);
    static char const* rejectCodeId(RejectCode candidate);
    static bool parseRejectCode(std::string const& s, RejectCode& out);
    static char const* attributionId(Attribution firstValue);
    static bool parseAttribution(std::string const& s, Attribution& out);

    static std::string clampDetail(std::string text);

    // One note per delay in 0..limit (empty if limit < 0).
    static Ledger build(BuildInput const& in);

    // Attribution codes for a press cycle (may be empty).
    static std::vector<AttributionNote> buildAttribution(AttributionInput const& in);

    static bool selfCheck(std::string& report);

private:
    friend class EbDevAttribution;

    friend class EbDevRejectChecks;

    EbDevRejectLedger() = delete;

    static RejectCode classifyRejected(CandSnap const& snap, CandSnap const* chosen,
                                       BuildInput const& in, int delay);
    static RejectCode skipCode(EarlyExit early);
};

#endif
