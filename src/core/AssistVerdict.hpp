#ifndef ASSIST_VERDICT_HPP
#define ASSIST_VERDICT_HPP

#include <string>

// Machine-readable Pass / Delay reason for planner + controller gates.
// Every shipped Pass path must set a known reason (PassUnknown = bug).
enum class AssistReason {
    None = 0,

    // Planner
    PassInvalidArgs,
    PassNoThreat,
    PassWaveProximity,
    PassWaveFutile,
    PassD0Safe,
    PassHopeless,
    PassBudget,

    // Controller gates (before / around plan)
    PassSpam,
    PassUntrust,
    PassLowTrust,
    PassLimitZero,
    PassDebounce,
    PassAssistOff,
    PassScope,
    PassEarlyAttempt,
    PassCps,

    // Bake
    PassBakeMissing,
    PassBakeNoWindow,
    PassBakePastLatest,
    PassBakeVerifyFail,
    PassBakeInWindow,

    // Delay outcomes
    DelayOrb,
    DelayThreat,
    DelayPrefer,
    DelayGeneric,

    // Soft hold (later commits may set; reserved)
    HoldPending,

    // Must stay last — selfCheck walks None..PassUnknown-1 then Unknown
    PassUnknown
};

class AssistVerdict {
public:
    // Stable debug / log token (never null).
    static char const* name(AssistReason reason);

    // Short overlay fragment, e.g. "PASS no-threat", "DELAY orb". Empty for None.
    static char const* overlayBit(AssistReason reason);

    static bool isPass(AssistReason reason);
    static bool isDelay(AssistReason reason);
    static bool isHold(AssistReason reason);

    // True for every named reason except None / PassUnknown.
    static bool isKnown(AssistReason reason);

    // Classify a finished SimPlan-like pair into Delay* when reason still None.
    static AssistReason inferDelayReason(
        bool activatedRing, bool requireOrb, bool usedPreferBand
    );

    static bool selfCheck(std::string& report);

private:
    AssistVerdict() = default;
};

#endif // ASSIST_VERDICT_HPP
