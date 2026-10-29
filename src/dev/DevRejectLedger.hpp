#ifndef DEV_REJECT_LEDGER_HPP
#define DEV_REJECT_LEDGER_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Per-delay reject ledger + attribution (Commit 12 / Vertrag D).
class DevRejectLedger {
public:
    static constexpr int kMaxCandNotes = 64;
    static constexpr int kMaxDetailLen = 120;

    enum class Status {
        Chosen = 0,
        Evaluated,
        Rejected,
        Skipped
    };

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
        LearningDisplaced
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

    struct CandSnap {
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
    };

    enum class EarlyExit {
        None = 0,
        Budget,
        PreferBand,
        WaveProximity,
        NoThreat,
        WaveFutile
    };

    struct BuildInput {
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
    };

    struct CandNote {
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
    };

    struct Ledger {
        std::vector<CandNote> notes;
        bool truncated = false; // scores omitted past kMaxCandNotes
    };

    struct AttributionInput {
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
    };

    struct AttributionNote {
        Attribution code = Attribution::GatePass;
        std::string detail;
    };

    static char const* statusId(Status s);
    static bool parseStatus(std::string const& s, Status& out);
    static char const* rejectCodeId(RejectCode c);
    static bool parseRejectCode(std::string const& s, RejectCode& out);
    static char const* attributionId(Attribution a);
    static bool parseAttribution(std::string const& s, Attribution& out);

    static std::string clampDetail(std::string s);

    // One note per delay in 0..limit (empty if limit < 0).
    static Ledger build(BuildInput const& in);

    // Attribution codes for a press cycle (may be empty).
    static std::vector<AttributionNote> buildAttribution(AttributionInput const& in);

    static bool selfCheck(std::string& report);

private:
    DevRejectLedger() = delete;

    static RejectCode classifyRejected(
        CandSnap const& snap,
        CandSnap const* chosen,
        BuildInput const& in,
        int delay
    );
    static RejectCode skipCode(EarlyExit early);
};

#endif // DEV_REJECT_LEDGER_HPP
