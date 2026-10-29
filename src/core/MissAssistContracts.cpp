#include "MissAssistContracts.hpp"

#include <algorithm>

int MissAssistContracts::clampDelayToWindow(int delay, int limit) {
    if (limit <= 0) return 0;
    if (delay < 0) return 0;
    if (delay > limit) return limit;
    return delay;
}

int MissAssistContracts::fireDeadline(int pressTick, int limit) {
    int const lim = std::max(0, limit);
    if (pressTick > 0x7fffffff - lim) return 0x7fffffff;
    return pressTick + lim;
}

MissAssistContracts::ExpectAction MissAssistContracts::legacyExpect(
    bool d0Viable, int earliestViableDelay
) {
    if (d0Viable) return MissAssistContracts::ExpectAction::Pass;
    if (earliestViableDelay > 0) return MissAssistContracts::ExpectAction::Delay;
    return MissAssistContracts::ExpectAction::Pass;
}

int MissAssistContracts::legacyDelay(
    bool d0Viable, int earliestViableDelay, int limit
) {
    if (d0Viable) return 0;
    if (earliestViableDelay <= 0) return 0;
    return MissAssistContracts::clampDelayToWindow(earliestViableDelay, limit);
}

MissAssistContracts::ExpectAction MissAssistContracts::newExpect(
    int bestInWindow, int limit
) {
    int const d = MissAssistContracts::clampDelayToWindow(bestInWindow, limit);
    if (d > 0) return MissAssistContracts::ExpectAction::Delay;
    return MissAssistContracts::ExpectAction::Pass;
}

int MissAssistContracts::newDelay(int bestInWindow, int limit) {
    return MissAssistContracts::clampDelayToWindow(bestInWindow, limit);
}

bool MissAssistContracts::preferBandEarlyExitAllowed(
    bool orbAssist, bool orbHintFound, bool spatialHint, bool foresightOrbPad
) {
    if (!orbAssist) return true;
    if (orbHintFound || spatialHint || foresightOrbPad) return false;
    return true;
}

// Table mirrors docs/assist_reference.md "Miss-assist contracts".
// idealOutside may exceed limit — assist must still clamp.
static MissAssistContracts::EarlyPressCase const s_cases[] = {
    {
        "early10-limit16-d0-safe-better10",
        16, 10, 10, true, 10,
        MissAssistContracts::ExpectAction::Pass, 0,
        MissAssistContracts::ExpectAction::Delay, 10,
        AssistReason::PassD0Safe
    },
    {
        "ideal6-limit4-best4",
        4, 6, 4, false, 4,
        MissAssistContracts::ExpectAction::Delay, 4,
        MissAssistContracts::ExpectAction::Delay, 4,
        AssistReason::None
    },
    {
        "ideal6-limit4-d0-safe-best4",
        4, 6, 4, true, 4,
        MissAssistContracts::ExpectAction::Pass, 0,
        MissAssistContracts::ExpectAction::Delay, 4,
        AssistReason::PassD0Safe
    },
    {
        "empty-corridor",
        16, 0, 0, true, -1,
        MissAssistContracts::ExpectAction::Pass, 0,
        MissAssistContracts::ExpectAction::Pass, 0,
        AssistReason::PassNoThreat
    },
    {
        "spike-d0-dies-earliest8",
        16, 8, 8, false, 8,
        MissAssistContracts::ExpectAction::Delay, 8,
        MissAssistContracts::ExpectAction::Delay, 8,
        AssistReason::None
    },
    {
        "hopeless-in-window",
        8, 20, 0, false, -1,
        MissAssistContracts::ExpectAction::Pass, 0,
        MissAssistContracts::ExpectAction::Pass, 0,
        AssistReason::PassHopeless
    },
    {
        "ten-display-frames-outside-limit16",
        16, 40, 0, true, -1,
        MissAssistContracts::ExpectAction::Pass, 0,
        MissAssistContracts::ExpectAction::Pass, 0,
        AssistReason::PassNoThreat
    },
    {
        "orb-activation-best12-d0-safe",
        16, 12, 12, true, 12,
        MissAssistContracts::ExpectAction::Pass, 0,
        MissAssistContracts::ExpectAction::Delay, 12,
        AssistReason::PassD0Safe
    },
};

static int const s_caseCount =
    static_cast<int>(sizeof(s_cases) / sizeof(s_cases[0]));

int MissAssistContracts::caseCount() {
    return s_caseCount;
}

MissAssistContracts::EarlyPressCase const& MissAssistContracts::caseAt(int index) {
    if (index < 0 || index >= s_caseCount) {
        return s_cases[0];
    }
    return s_cases[index];
}

bool MissAssistContracts::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* label) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += ", ";
        report += label;
    };

    expect(MissAssistContracts::clampDelayToWindow(6, 4) == 4, "clamp-6-to-4");
    expect(MissAssistContracts::clampDelayToWindow(3, 4) == 3, "clamp-3-in-4");
    expect(MissAssistContracts::clampDelayToWindow(0, 4) == 0, "clamp-0");
    expect(MissAssistContracts::clampDelayToWindow(-2, 4) == 0, "clamp-neg");
    expect(MissAssistContracts::clampDelayToWindow(8, 0) == 0, "clamp-limit0");
    expect(MissAssistContracts::clampDelayToWindow(8, -1) == 0, "clamp-limit-neg");
    expect(MissAssistContracts::clampDelayToWindow(16, 16) == 16, "clamp-eq");

    expect(MissAssistContracts::fireDeadline(100, 16) == 116, "deadline-116");
    expect(MissAssistContracts::fireDeadline(0, 8) == 8, "deadline-8");
    expect(MissAssistContracts::fireDeadline(50, 0) == 50, "deadline-limit0");

    expect(
        MissAssistContracts::legacyExpect(true, 10) == ExpectAction::Pass,
        "legacy-d0-pass"
    );
    expect(
        MissAssistContracts::legacyDelay(true, 10, 16) == 0,
        "legacy-d0-delay0"
    );
    expect(
        MissAssistContracts::legacyExpect(false, 8) == ExpectAction::Delay,
        "legacy-earliest-delay"
    );
    expect(
        MissAssistContracts::legacyDelay(false, 8, 16) == 8,
        "legacy-earliest-8"
    );
    expect(
        MissAssistContracts::legacyDelay(false, 20, 16) == 16,
        "legacy-clamp-20-to-16"
    );
    expect(
        MissAssistContracts::legacyExpect(false, -1) == ExpectAction::Pass,
        "legacy-hopeless-pass"
    );

    expect(
        MissAssistContracts::newExpect(10, 16) == ExpectAction::Delay,
        "new-best10"
    );
    expect(MissAssistContracts::newDelay(10, 16) == 10, "new-delay10");
    expect(
        MissAssistContracts::newExpect(0, 16) == ExpectAction::Pass,
        "new-best0-pass"
    );
    expect(MissAssistContracts::newDelay(6, 4) == 4, "new-clamp-6-to-4");
    expect(
        MissAssistContracts::newExpect(6, 4) == ExpectAction::Delay,
        "new-clamped-still-delay"
    );

    // Legacy-only helper: New pick never early-exits (wired in plan()).
    expect(
        MissAssistContracts::preferBandEarlyExitAllowed(false, true, true, true),
        "prefer-exit-no-orb-assist"
    );
    expect(
        MissAssistContracts::preferBandEarlyExitAllowed(true, false, false, false),
        "prefer-exit-orb-idle"
    );
    expect(
        !MissAssistContracts::preferBandEarlyExitAllowed(true, true, false, false),
        "prefer-exit-block-hint"
    );
    expect(
        !MissAssistContracts::preferBandEarlyExitAllowed(true, false, true, false),
        "prefer-exit-block-spatial"
    );
    expect(
        !MissAssistContracts::preferBandEarlyExitAllowed(true, false, false, true),
        "prefer-exit-block-foresight"
    );

    expect(MissAssistContracts::caseCount() >= 6, "case-count");
    for (int i = 0; i < MissAssistContracts::caseCount(); ++i) {
        auto const& c = MissAssistContracts::caseAt(i);
        expect(c.name != nullptr && c.name[0] != '\0', "case-name");
        expect(c.limit >= 0, "case-limit");

        int const newD = MissAssistContracts::newDelay(c.bestInWindow, c.limit);
        expect(newD <= c.limit, "case-new-leq-limit");
        expect(newD == c.newDelayExpect, "case-new-delay");
        expect(
            MissAssistContracts::newExpect(c.bestInWindow, c.limit) == c.newAction,
            "case-new-action"
        );

        int const legD = MissAssistContracts::legacyDelay(
            c.d0Viable, c.earliestViable, c.limit
        );
        expect(legD <= c.limit, "case-legacy-leq-limit");
        expect(legD == c.legacyDelayExpect, "case-legacy-delay");
        expect(
            MissAssistContracts::legacyExpect(c.d0Viable, c.earliestViable)
                == c.legacyAction,
            "case-legacy-action"
        );

        expect(
            MissAssistContracts::fireDeadline(0, c.limit) == c.limit,
            "case-deadline"
        );

        if (c.passReasonHint != AssistReason::None) {
            expect(AssistVerdict::isKnown(c.passReasonHint), "case-reason-known");
            if (c.legacyAction == ExpectAction::Pass
                || c.newAction == ExpectAction::Pass) {
                expect(AssistVerdict::isPass(c.passReasonHint), "case-reason-pass");
            }
        }
    }

    expect(
        MissAssistContracts::newDelay(6, 4) == 4
            && MissAssistContracts::legacyDelay(false, 6, 4) == 4,
        "g1-never-beyond-window"
    );

    {
        auto const& c = MissAssistContracts::caseAt(0);
        expect(
            c.legacyAction == ExpectAction::Pass
                && c.newAction == ExpectAction::Delay
                && c.newDelayExpect == 10,
            "early10-new-vs-legacy"
        );
    }

    if (fails == 0) {
        report = "MissAssistContracts selfCheck OK";
        return true;
    }
    report = std::string("MissAssistContracts selfCheck fails: ") + report;
    return false;
}
