#ifndef MISS_ASSIST_CONTRACTS_HPP
#define MISS_ASSIST_CONTRACTS_HPP

#include "AssistVerdict.hpp"

#include <string>

// Pure logic contracts for early-press / miss-assist (no PlayLayer).
// PlanGold + future pick modes (Legacy vs New) share these fixtures.
class MissAssistContracts {
public:
    enum class PickMode {
        Legacy,
        New
    };

    enum class ExpectAction {
        Pass,
        Delay
    };

    // G1 hard window cap: result always in [0, limit]. limit <= 0 → 0.
    static int clampDelayToWindow(int delay, int limit);

    // Absolute fire deadline: pressTick + limit (limit <= 0 → pressTick).
    static int fireDeadline(int pressTick, int limit);

    // Legacy (1.2.0 feel): d0 viable → Pass. Else earliest viable in window.
    // earliestViableDelay: -1 = none; if >= 0 must already be intended in-window.
    static ExpectAction legacyExpect(bool d0Viable, int earliestViableDelay);
    static int legacyDelay(bool d0Viable, int earliestViableDelay, int limit);

    // New (prefer-delay): bestInWindow already picked among 0..limit (0 = Pass now).
    static ExpectAction newExpect(int bestInWindow, int limit);
    static int newDelay(int bestInWindow, int limit);

    // Prefer-band early-exit (C10): allowed only when orb foresight/window is idle.
    // When orbAssist && (orbHint|spatial|foresight), keep searching past prefer so
    // activations outside the bake prefer band are still evaluated (Orb before Prefer).
    static bool preferBandEarlyExitAllowed(
        bool orbAssist, bool orbHintFound, bool spatialHint, bool foresightOrbPad
    );

    // One synthetic early-press row for table-driven selfCheck / docs.
    struct EarlyPressCase {
        char const* name = "";
        int limit = 0;
        int idealOutside = 0;       // "true" best may lie past window (informational)
        int bestInWindow = 0;       // best viable among 0..limit (New pick input)
        bool d0Viable = false;
        int earliestViable = -1;    // Legacy earliest viable (-1 none)
        ExpectAction legacyAction = ExpectAction::Pass;
        int legacyDelayExpect = 0;
        ExpectAction newAction = ExpectAction::Pass;
        int newDelayExpect = 0;
        AssistReason passReasonHint = AssistReason::None; // docs / overlay mapping
    };

    static int caseCount();
    static EarlyPressCase const& caseAt(int index);

    static bool selfCheck(std::string& report);

private:
    MissAssistContracts() = default;
};

#endif // MISS_ASSIST_CONTRACTS_HPP
