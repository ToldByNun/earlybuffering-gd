#ifndef ASSIST_SIM_ESTIMATE_HPP
#define ASSIST_SIM_ESTIMATE_HPP

#include "../assist/contracts/assist_contracts140.hpp"
#include <string>
#include <vector>

// Commit 12 — Counterfactual Net as simEstimate only (G10).
// Never a ship Pass/Fail by itself. Real gates stay on AssistRealMetrics.
class AssistSimEstimate {
public:
    enum class NetClass {
        Neutral = 0,
        Prev,        // armed Delay survives primary; d0 dies
        Caused,      // armed Delay dies primary; d0 survives
        Inconclusive // unknown / budget / aborted on either branch
    };

    class SampleView {
    public:
        int delay = 0;
        bool survivedPrimary = false;
        bool inconclusive = false;

    private:
    };

    static AssistSimEstimate& get();

    void resetSession();

    // Pure: compare d0 vs planned Delay primary survival.
    static NetClass classifyArm(bool d0SurvivedPrimary, bool d0Inconclusive,
                                bool delaySurvivedPrimary, bool delayInconclusive,
                                int plannedDelay);

    // Find d0 + plannedDelay in a grid; classify. plannedDelay<=0 → Neutral.
    static NetClass classifyFromSamples(std::vector<SampleView> const& samples, int plannedDelay);

    static char const* netClassId(NetClass candidate);

    // Claims-allowed eval of an armed Delay — bump Prev/Caused counters.
    void noteEval(NetClass candidate, bool claimsAllowed);

    int prev() const {
        return this->m_prev;
    }
    int caused() const {
        return this->m_caused;
    }
    int neutral() const {
        return this->m_neutral;
    }
    int inconclusive() const {
        return this->m_inconclusive;
    }
    int evalArms() const {
        return this->m_evalArms;
    }

    int net() const {
        return this->m_prev - this->m_caused;
    }
    // HarmRate_sim = Caused / max(1, evalArms). Not a ship gate (G10).
    float harmRateSim() const;

    // Always tagged simEstimate — never mixed into AssistRealMetrics::reportLine.
    std::string reportLine() const;
    // Overlay: "simEst net=+2/-1 harm=0.05" or empty when no evals.
    std::string overlayBit() const;

    static bool selfCheck(std::string& report);

private:
    AssistSimEstimate() = default;

    int m_prev = 0;
    int m_caused = 0;
    int m_neutral = 0;
    int m_inconclusive = 0;
    int m_evalArms = 0; // claims-allowed evals with plannedDelay > 0
};

#endif
