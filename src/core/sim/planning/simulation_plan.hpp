#ifndef SIMULATION_PLAN_HPP
#define SIMULATION_PLAN_HPP

#include "../../../config/stage_config.hpp"
#include "../../../dev/evaluation/dev_reject_ledger.hpp"
#include "../../assist/contracts/assist_verdict.hpp"
#include <Geode/Geode.hpp>
#include <vector>

enum class PlanAction {
    Pass, // do the input now (vanilla) — NEVER block the player
    Delay // wait `delay` ticks, then do it (assist only)
};

class EbThreatZone {
public:
    float startX = 0.f;
    float endX = 0.f;
    bool valid = false;

private:
};

class EbPlanOptions {
public:
    float capMul = 1.f;
    int maxBranches = 4;
    int budgetUs = 1500;   // soft cap; tick budget is primary
    int tickBudget = 4000; // physics ticks per plan() call
    int smartDepth = 1;
    int smartCombos = 48;
    int smartHorizon = 0;     // 0 = auto from model cap
    int attemptStartTick = 0; // absolute attempt tick at plan time (for abs death ticks)
    // Live bake-prior: delay ticks from a nearby BakeMap window (−1 = unset).
    // Enqueued after d0 / orb window; early-exit when a ±k-robust prefer delay is viable.
    int preferDelayMin = -1;
    int preferDelayMax = -1;
    // ±k robust margin: 0 = auto (mode/speed/dtVariance/spam); else clamp 1–3.
    int robustK = 0;
    bool robustJitter = false;
    float dtVariance = 0.f; // relative dt EMA from AssistController (auto k bump)
    // betterCand after surviveBin: endX − latencyWeight · delay.
    float latencyWeight = 0.25f;
    // ClickIntentGuard density (assist still running, e.g. orb) → wider auto k,
    // slightly softer Wave λ so earliest-delay bias does not phase-shift spam.
    bool spamDense = false;
    // true = assist-pick v1; false = v2 slot (AssistPickV2 TBD — currently v1 choose).
    bool legacyPick = false;
    // Live V2 sim backend (WU8). V1 effective = ShadowPlayer always.
    EbStageConfig::AssistEngineKind assistEngine = EbStageConfig::AssistEngineKind::ShadowPlayer;
    // Commit 11: PerfectAssistPolicy MustDelaySearch — skip PassNoThreat early exit.
    bool mustDelaySearch = false;
    // Live Delay-arm: only Class::Alive ghosts (soft Landing/Recoverable/LateCrash
    // stay not-false-lethal for bake verify when this is false).
    bool requireAliveForDelay = false;
    // Hybrid verify-fail: reuse threat zone from verifyDelay (skip buildThreatZone).
    // Invalid / unset → plan builds zone from d0 as usual. Never stale across presses.
    EbThreatZone seedThreatZone{};

private:
};

class EbActWindow {
public:
    int dMin = -1;
    int dMax = -1;
    bool valid() const {
        return dMin >= 0 && dMax >= dMin;
    }

private:
};

class EbSimPlan {
public:
    bool fixedFireTick = false;
    PlanAction action = PlanAction::Pass;
    AssistReason reason = AssistReason::None;
    int delay = 0;
    int jumpTick = 0;
    bool nowIsSafe = false;
    bool continuous = false;
    int candidatesTried = 0;
    int firstDeathTick = -1;
    GameObject* firstDeathObject = nullptr;
    int baselineDeathTick = -1;
    GameObject* baselineDeathObject = nullptr;
    float baselineDeathX = 0.f;
    bool unknown = false;
    // True when sim overlapped a toggle-orb / touch-toggle target (Commit 3).
    bool toggleUnknown = false;
    bool budgetHit = false;
    bool activatedRing = false;
    bool usedOrbWindow = false;
    bool foresightOrbPad = false; // TravelForesight orb/pad in travel cone
    bool threatCleared = false;
    bool orbUseful = false;
    // Continuous miss-assist: idle (no press) safer than d0 press, and some
    // later delay in-window improves survival (ascent/hold). → Delay not Pass.
    bool idleSafer = false;
    bool ascentUseful = false;
    // Live V2 LongForesight: press dies on long horizon → choose prefers Delay.
    bool longForesightNeedSearch = false;
    // Live V2: plan cand eval used Trajectory engine (else ShadowPlayer).
    bool trajectoryEngine = false;
    bool requireOrb = false;
    bool requireHold = false;
    int dMin = -1;
    int dMax = -1;
    // Commit 8: exact Delay-viable set for planSequence exit-predict (not a filled band).
    std::vector<int> viableDelays;
    // Hold/release after assisted press (ticks after the press action).
    // earliest..latest = safe hold durations; preferred = best release timing.
    // -1 = unset (no probe / Pass).
    int releaseAfter = -1;
    int releaseEarliest = -1;
    int releaseLatest = -1;
    int holdScanTried = 0;
    int planId = 0;
    int smartDepthUsed = 1;
    int smartCombosUsed = 0;
    EbThreatZone threatZone{};
    // Commit 12: one note per delay 0..limit after plan/planSequence entry.
    std::vector<EbDevRejectLedger::CandNote> candNotes;
    bool candNotesTruncated = false;

private:
};

class EbInputEvent {
public:
    int tick = 0; // absolute attempt tick
    bool down = false;

private:
};

#endif
