#ifndef DEV_REPORT_HPP
#define DEV_REPORT_HPP

class DevDecisionEvent;
class DevAttempt;

#include "dev_eval_engine.hpp"
#include "dev_objective.hpp"
#include "dev_state_diff.hpp"
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

    // Commit 12: counterfactual Prev/Caused class for this eval (simEstimate / G10).
    int plannedDelay = -1;
    char const* simNetClass = "neutral"; // prev|caused|neutral|inconclusive
    int simPrev = 0;
    int simCaused = 0;
    int simNet = 0;
    float simHarmRate = 0.f;

    // Build from an eval grid result. Never writes ideal= unless idealEligible.
    // plannedDelay > 0 → accumulate AssistSimEstimate (secondary, not a ship gate).
    static DevReport fromGrid(DevEvalEngine::GridReport const& grid, std::string recLine,
                              int plannedDelay = -1);

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

private:
};

#include <Geode/Geode.hpp>
#include <cstring>
#include <fmt/format.h>

class DevReportImplementation {
public:
    static char const* fidShort(DevFidelity frame, bool approx);
    static DevObjective::Kind objectiveKindFromConfig();
    static DevDecisionEvent const* findPlanFinished(DevAttempt const& attempt);
    static int limitFromPlanEvent(DevDecisionEvent const& e);
    static bool runEvalOnPlanEvent(PlayLayer* layer, DevDecisionEvent const& planEv, DevReport& out,
                                   std::string& err);

private:
    friend class DevReport;

    static DevReport s_lastReport;
    static bool s_hasLastReport;

    DevReportImplementation() = delete;
};

#endif
