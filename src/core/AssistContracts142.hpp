#ifndef EARLYBUFFER_CORE_ASSISTCONTRACTS142_HPP
#define EARLYBUFFER_CORE_ASSISTCONTRACTS142_HPP

#include "AssistContracts140.hpp"

#include <string>

// v1.4.2 contract sheet (Route / SpamAssist / ModeAssist).
// Status: UNFROZEN — thresholds stay placeholders until a measured corpus fill.
// Do not retune AssistContracts140 numbers here; that sheet stays LOCKED.
class AssistContracts142 {
public:
    static constexpr int kSchema = 1;
    static constexpr char const* kCorpusLabel = "solo";
    // True until measured numbers replace placeholders below.
    static constexpr bool kUnfrozen = true;

    // Sample sizes (placeholders — treat gates as INCONCLUSIVE while unfrozen).
    static constexpr int kAMinSolo = 40;
    static constexpr int kAMinMulti = 100;
    static constexpr int kOppMinPerLevel = 20;
    static constexpr int kAMinPerModeSolo = 12; // per GameModeKind before mode gate matters

    // Placeholder ship bounds (NOT LOCKED). Recording only while kUnfrozen.
    static constexpr float kR3DafCiUpperMax = 0.02f;
    static constexpr float kR1AssistCovMin = 0.95f;
    static constexpr float kR2TrustRateStaticMin = 0.70f;
    static constexpr float kR7P95PlanUsSlack = 1.0f;

    enum class GateId {
        R1AssistCov = 0,
        R2TrustRate,
        R3Daf,
        R4RouteDirection,
        R5OffRouteFallback,
        R6PathStale,
        R7PlanP95,
        R8CacheLoadP95,
        Count
    };

    static char const* gateIdName(GateId id);
    static char const* freezeStatus(); // "UNFROZEN" or "LOCKED"

    // While unfrozen: always Inconclusive (measurement sheet, not ship fail).
    static AssistContracts140::GateStatus gateR1AssistCov(int arms, int trustOpp);
    static AssistContracts140::GateStatus gateR2TrustRate(
        int trustOpp, int opp, bool dynamicLabelled
    );
    static AssistContracts140::GateStatus gateR3Daf(
        int daf, int arms, char const* corpusLabel
    );

    // Report-only stubs (always Inconclusive while unfrozen / until wired).
    static AssistContracts140::GateStatus gateReportOnly();

    static int aMinForCorpus(char const* corpusLabel);
    static int aMinPerMode(char const* corpusLabel);

    static bool selfCheck(std::string& report);

private:
    AssistContracts142() = delete;
};

#endif // EARLYBUFFER_CORE_ASSISTCONTRACTS142_HPP
