#ifndef ADAPTIVE_DECISION_GATE_HPP
#define ADAPTIVE_DECISION_GATE_HPP

#include "../../../../assist/contracts/assist_verdict.hpp"
#include "../../simulation_plan.hpp"
#include "adaptive_candidate_slot.hpp"
#include <string>

// Phase D — Assist only after validated Autonomous Survival + hazard progress.
// A larger deathTick / surviveTicks alone must never arm Delay.
// Recoverability is ignored for arming (AP8 diagnostic only).
class EbAdaptiveDecisionGate {
public:
    class CandView {
    public:
        bool screened = false;
        bool autonomousAlive = false;
        bool terminalDied = false;
        bool terminalUnknown = false;
        bool terminalAliveCap = false;
        bool zoneCleared = false;
        bool activatedRing = false;
        bool recoverability = false; // never sufficient alone
        int ticksSimulated = 0;
        int autonomousSurviveTicks = 0;
        int deathTick = -1;
        float endX = 0.f;

    private:
    };

    class Input {
    public:
        CandView const* cands = nullptr;
        int limit = 0;
        int modelCap = 1;
        int robustK = 0;
        float latencyWeight = 0.25f;
        bool mustDelaySearch = false;
        bool budgetHitIncomplete = false;
        bool activatedRingPlan = false;
        int screenProofTicks = 24; // Alive min-proof; align with adaptiveScreenTicks

    private:
    };

    class Result {
    public:
        PlanAction action = PlanAction::Pass;
        AssistReason reason = AssistReason::PassHopeless;
        int delay = 0;
        bool nowIsSafe = false;
        bool threatCleared = false;
        bool requireOrb = false;
        int jumpTick = 0;
        bool activatedRing = false;

    private:
    };

    static CandView fromSlot(EbAdaptiveCandidateSlot const& slot);

    // Live Delay-worthy: still living autonomous ghost, no unknown.
    static bool isDelayWorthy(CandView const& c);

    // Hazard progress for Delay arming (never declares d0 safe by itself).
    // screenProofTicks: Alive floor when beating a dying d0 (matches screen quantum).
    static bool hasHazardProgress(CandView const& cand, CandView const& d0, int modelCap,
                                  int screenProofTicks = 24);

    // PassD0Safe proof: horizon / cap / zone only — screen min-proof is not enough.
    static bool isProvenSafe(CandView const& cand, int modelCap);

    static bool neighborsRobust(CandView const* cands, int limit, int robustK, int delay);

    // Rank among gate-eligible delays: hazard-progress already required.
    // Prefer orb activation, then endX − λ·delay, then earlier delay.
    // Explicitly does NOT compare surviveTicks / deathTick.
    static bool rankBetter(CandView const& a, int delayA, CandView const& b, int delayB,
                           float latencyWeight);

    static Result decide(Input const& in);

    static bool selfCheck(std::string& report);

private:
    EbAdaptiveDecisionGate() = delete;
};

#endif
