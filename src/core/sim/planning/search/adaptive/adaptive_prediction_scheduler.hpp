#ifndef ADAPTIVE_PREDICTION_SCHEDULER_HPP
#define ADAPTIVE_PREDICTION_SCHEDULER_HPP

#include "adaptive_candidate_slot.hpp"
#include <string>

// Picks the next Adaptive Phase C grant by information gain.
// Pure over slots + remaining verify/reserve budget — no PlayLayer.
// Does not raise Max Assist or Prediction Horizon.
class EbAdaptivePredictionScheduler {
public:
    enum class ActionKind {
        StopPassD0Safe = 0, // d0 already survives to horizon — no Delay
        StopHopeless = 1,   // no viable autonomous survivor
        StopBudget = 2,     // Dynamic Budget empty mid-verify
        StopUnknown = 3,    // survivors exist but none proved through horizon
        ContinueVerify = 4, // grant another quantum to `delay`
        FallThrough = 5     // AP5: enough for classic / later DecisionGate
    };

    class Decision {
    public:
        ActionKind kind = ActionKind::FallThrough;
        int delay = -1; // set when ContinueVerify

    private:
    };

    class Input {
    public:
        EbAdaptiveCandidateSlot const* slots = nullptr;
        int limit = 0;
        int modelCap = 1;
        int verifyLeft = 0;
        int reserveLeft = 0;
        int closeGapTicks = 8; // two leads within this Autonomous gap → split
        bool mustDelaySearch = false;

    private:
    };

    static Decision choose(Input const& in);

    // Information-gain score for a living under-horizon candidate (−1 = ineligible).
    static int scoreCandidate(EbAdaptiveCandidateSlot const& slot,
                              EbAdaptiveCandidateSlot const& d0, int modelCap);

    static bool selfCheck(std::string& report);

private:
    EbAdaptivePredictionScheduler() = delete;
};

#endif
