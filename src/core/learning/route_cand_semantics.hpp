#ifndef ROUTE_CAND_SEMANTICS_HPP
#define ROUTE_CAND_SEMANTICS_HPP

#include "ai_route.hpp"
#include <cstdint>
#include <string>
#include <vector>

// Assist Pack AP-NL3 — first-party cand / expand-job / route lifetime (DR2).
// Nova MacroCand≈232 and expand-job≈1856 are research sizes only — never
// sizeof targets, packing goals, or allocator stride matches.
class EbRouteCandSemantics {
public:
    enum class LifeState : std::uint8_t {
        Proposed = 0,
        Expanding,
        PartiallyChecked,
        ShadowVerified,
        Rejected,
        Exhausted,
        Armed
    };

    // Semantic candidate (Nova MacroCand fields mapped by meaning, not bytes).
    class Cand {
    public:
        std::uint32_t id = 0;
        EbAiRouteEvent::Kind kind = EbAiRouteEvent::Kind::Press;
        int delayTicks = 0;  // relative to plan tick
        int holdTicks = 0;   // planned hold for Press seeds
        float neuralScore = 0.f;
        float progressScore = 0.f;
        bool clearsThreat = false;
        bool unknown = false;
        bool budgetHit = false;
        bool simOk = false;
        LifeState state = LifeState::Proposed;
        std::uint32_t expandJobId = 0; // 0 = unbound

    private:
    };

    // Expand job owns a seed + grown event list (Nova 1856-record semantics).
    class ExpandJob {
    public:
        std::uint32_t id = 0;
        std::uint32_t seedCandId = 0;
        int planTick = -1;
        int horizonTicks = 0;
        int lastEventDelay = 0;
        bool budgetHit = false;
        bool unknown = false;
        LifeState state = LifeState::Proposed;
        std::vector<Cand> events;

    private:
    };

    static char const* stateName(LifeState state);
    static bool canTransition(LifeState from, LifeState to);
    static bool applyTransition(Cand& cand, LifeState to);
    static bool applyJobTransition(ExpandJob& job, LifeState to);

    // Seed from neural proposal (Autoplay handoff). Live may diagnose read-only.
    static Cand makeSeed(std::uint32_t id, int delayTicks, float neuralScore, int holdTicks = 0);

    // Begin expand around a Proposed/Expanding seed.
    static ExpandJob beginExpand(std::uint32_t jobId, Cand const& seed, int planTick,
                                 int horizonTicks);

    // Record one simulated event under the job (press/release).
    static bool noteSimEvent(ExpandJob& job, Cand& event, bool simOk, bool unknown, bool budgetHit,
                             float progress, bool clearsThreat);

    // After expand: ShadowVerified only if every event simOk, horizon covers last
    // event, and neither unknown nor budgetHit. Else PartiallyChecked / Rejected.
    static LifeState finalizeExpand(ExpandJob& job);

    // DR3 / AP-NL5b: arm only from ShadowVerified (see EbShadowVerifiedContract).
    static bool canArm(Cand const& cand);
    static bool canArmJob(ExpandJob const& job);

    // Convert finalized job events → EbAiRouteEvent list (Autoplay binder).
    static std::vector<EbAiRouteEvent> toRouteEvents(ExpandJob const& job);

    // Live diagnostics — read-only strings (no mutate).
    static std::string diagnoseCand(Cand const& cand);
    static std::string diagnoseJob(ExpandJob const& job);

    // Research note: Nova strides are documented for field discovery only.
    static char const* novaMacroCandResearchNote();
    static char const* novaExpandJobResearchNote();

    static bool selfCheck(std::string& report);

private:
    EbRouteCandSemantics() = delete;
};

#endif
