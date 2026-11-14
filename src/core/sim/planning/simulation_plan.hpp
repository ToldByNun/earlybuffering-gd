#ifndef SIMULATION_PLAN_HPP
#define SIMULATION_PLAN_HPP

#include "../../../config/stage_config.hpp"
#include "../../../dev/evaluation/dev_reject_ledger.hpp"
#include "../../assist/contracts/assist_verdict.hpp"
#include "simulation_candidate.hpp"
#include <Geode/Geode.hpp>
#include <chrono>
#include <string>
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

// Three hard limits stay strictly separated (Adaptive Prediction Planner):
//   1) Max Assist Ticks  — caller `limit` / ModeParams.ticks / fire deadline.
//      PlanOptions must never raise that window or invent extra real clicks.
//   2) Prediction Horizon — AssistHorizon / model.cap / scoreHorizon (capMul).
//      Adaptive may stop early inside the horizon; never raise it via budget.
//   3) Dynamic Plan Tick Budget — tickBudget + budgetUs (ceilings from settings).
//      Adaptive may redistribute / early-stop inside these; never treat leftover
//      CPU as permission to grow Max Assist or Prediction Horizon.
class EbPlanOptions {
public:
    float capMul = 1.f;
    int maxBranches = 4;
    int budgetUs = 4000;   // soft cap; tick budget is primary
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
    // Adaptive→classic fallthrough: terminal / horizon-proven delay results so
    // classic does not re-sim work Adaptive already finished. Observation-cleared
    // Alive screens seed only when ticks cover screen quantum AND any known
    // longer death (AP10). Shorter partial screens are NOT seeded.
    std::vector<EbSimulationCandidate> seedCandidates{};
    // Shared idle-death tick from Adaptive PrefixSim (−1 = none).
    int seedPrefixDeathTick = -1;
    // Adaptive Prediction Planner (AP0 contracts; search path lands in later APs).
    // When false, callers keep classic ShadowPlanSearch. Adaptive must not raise
    // Max Assist or Prediction Horizon from these knobs — only subdivide tickBudget.
    bool allowSubtick = false;
    bool adaptivePlan = false;
    int adaptiveScreenTicks = 24;   // Phase A/B short quantum per candidate
    int adaptiveVerifyQuantum = 16; // Phase C extend grant size
    int adaptiveReservePct = 15;    // Phase D / tie reserve of Dynamic Plan Tick Budget
    // Assist Pack AP-L5: Adaptive→classic fallthrough reuses the press soft-wall
    // clock (no fresh wall0 / no remUs floor stacked past sim-budget-us).
    bool reusePlanWall = false;
    std::chrono::steady_clock::time_point planWall0{};

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
    float firePhase = 0.f;
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
    // Wall µs spent in plan()/planSequence (filled by Live press/release planners).
    int planUs = 0;
    int physicsCalls = 0;
    int physicsWorldUs = 0;
    int physicsUpdateUs = 0;
    int physicsCollisionUs = 0;
    int worldCaptureUs = 0;
    int prepareUs = 0;
    int guardOriginalDelay = -1;
    int guardProbeCount = 0;
    bool guardProbeIncomplete = false;

    int adaptiveWorkUs = 0;
    int fallthroughSetupUs = 0;
    int classicSearchUs = 0;
    int adaptiveSeedCount = 0;
    int reusedCandidateCount = 0;
    int reusedCandidateTicks = 0;
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
    // Adaptive Prediction Planner shell (AP4+). Classic plans leave these false/0.
    bool adaptiveShell = false;       // Adaptive A/B/C ran under adaptive-plan
    bool adaptiveFellThrough = false; // needed DecisionGate/classic (until AP6 owns Delay)
    // Assist Pack AP-M6: Adaptive aborted after screen (no Alive) to gift remUs.
    bool adaptiveAbortedForFallthrough = false;
    int adaptiveScreenedCount = 0;     // delays that received a screen quantum
    int adaptiveSurvivors = 0;         // screened delays still autonomous-alive after screen
    int adaptiveVerifyGrants = 0;      // Phase C verify quanta granted
    int adaptiveStolenScreenTicks = 0; // Assist Pack AP2: Screen→Verify steal count
    // Dev Lab AP4: which search branch produced this plan.
    // inputWindow | threatProbe | adaptive | classic | hybridFallthrough
    std::string plannerPath;
    // Dev Lab AP7: Wave continuous IW proximity / help-only / futile honesty.
    bool hasWaveMeta = false;
    float waveProxX = 0.f;
    bool waveNearHazard = false;
    float waveBestGain = 0.f;
    int waveEarliestLater = -1;
    bool waveHelpOnly = false;
    bool waveFutile = false;
    bool waveProximityPass = false;

private:
};

class EbInputEvent {
public:
    int tick = 0; // absolute attempt tick
    bool down = false;

private:
};

#endif
