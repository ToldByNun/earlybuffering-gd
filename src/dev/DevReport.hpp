#ifndef DEV_REPORT_HPP
#define DEV_REPORT_HPP

#include "DevEvalEngine.hpp"
#include "DevObjective.hpp"
#include "DevStateDiff.hpp"

#include <string>

// Overlay line + last-report store (Commit 14). Field names are literal contract A.
class DevReport {
public:
    std::string recLine; // e.g. REC 3/10 or empty
    DevFidelity fidelity = DevFidelity::NotReproducible;
    bool claimsAllowed = false;
    bool approx = false;
    std::string objectiveId;
    int horizonTicks = 0;
    int tailTicks = 0;

    bool hasBestSurviving = false;
    int bestSurvivingDelay = -1;
    bool hasBestEvaluated = false;
    int bestEvaluatedDelay = -1;

    bool idealEligible = false;
    bool hasIdealDelay = false;
    int idealDelay = -1;
    std::string idealBlockedReason;

    std::string overlayLine;
    std::string jsonLine;
    bool valid = false;

    // Live armed a Delay but offline eval found no surviving delay (hallucination signal).
    bool liveVsEvalMismatch = false;
    int livePlannedDelay = -1;

    // Build from an eval grid result. Never writes ideal= unless idealEligible.
    static DevReport fromGrid(
        DevEvalEngine::GridReport const& grid,
        std::string recLine
    );

    // Pure formatter used by fromGrid and selfCheck.
    static std::string formatOverlayLine(DevReport const& r);

    static void setLast(DevReport report);
    static DevReport const& last();
    static bool hasLast();
    static void clearLast();

    // Live eval against TraceStore attempt(s). Needs attached PlayLayer + ghosts.
    static bool evalLastAttempt(std::string& err);
    static bool evalSession(std::string& err, int* outCount = nullptr);

    static bool selfCheck(std::string& report);
};

#endif // DEV_REPORT_HPP
