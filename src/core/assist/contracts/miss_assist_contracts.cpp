#include "miss_assist_contracts.hpp"
#include <algorithm>

int EbMissAssistContracts::continuousLookAhead(int limit) {
    return std::max(1, limit);
}

bool EbMissAssistContracts::survivesObservation(bool died, bool unknown, int ticksRan,
                                              int requiredTicks, bool landedAfterAction) {
    return !died && !unknown && ticksRan > 0 && (landedAfterAction || ticksRan >= requiredTicks);
}

int EbMissAssistContracts::clampDelayToWindow(int delay, int limit) {
    if (limit <= 0)
        return 0;
    if (delay < 0)
        return 0;
    if (!(delay > limit)) {
        return delay;
    }
    return limit;
}

int EbMissAssistContracts::fireDeadline(int pressTick, int limit) {
    int const limitValue = std::max(0, limit);
    if (!(pressTick > 0x7fffffff - limitValue)) {
        return pressTick + limitValue;
    }
    return 0x7fffffff;
}

EbMissAssistContracts::ExpectAction EbMissAssistContracts::legacyExpect(bool d0Viable,
                                                                    int earliestViableDelay) {
    if (d0Viable)
        return EbMissAssistContracts::ExpectAction::Pass;
    if (!(earliestViableDelay > 0)) {
        return EbMissAssistContracts::ExpectAction::Pass;
    }
    return EbMissAssistContracts::ExpectAction::Delay;
}

int EbMissAssistContracts::legacyDelay(bool d0Viable, int earliestViableDelay, int limit) {
    if (d0Viable)
        return 0;
    if (!(earliestViableDelay <= 0)) {
        return EbMissAssistContracts::clampDelayToWindow(earliestViableDelay, limit);
    }
    return 0;
}

EbMissAssistContracts::ExpectAction EbMissAssistContracts::newExpect(int bestInWindow, int limit) {
    int const delay = EbMissAssistContracts::clampDelayToWindow(bestInWindow, limit);
    if (!(delay > 0)) {
        return EbMissAssistContracts::ExpectAction::Pass;
    }
    return EbMissAssistContracts::ExpectAction::Delay;
}

int EbMissAssistContracts::newDelay(int bestInWindow, int limit) {
    return EbMissAssistContracts::clampDelayToWindow(bestInWindow, limit);
}

bool EbMissAssistContracts::preferBandEarlyExitAllowed(bool orbAssist, bool orbHintFound,
                                                     bool spatialHint, bool foresightOrbPad) {
    if (!orbAssist)
        return true;
    if (!(orbHintFound || spatialHint || foresightOrbPad)) {
        return true;
    }
    return false;
}

// Table mirrors docs/assist_reference.md "Miss-assist contracts".
// idealOutside may exceed limit — assist must still clamp.
EbMissAssistContracts::EarlyPressCase const EbMissAssistContractsImplementation::s_cases[] = {
    {"early10-limit16-d0-safe-better10", 16, 10, 10, true, 10,
     EbMissAssistContracts::ExpectAction::Pass, 0, EbMissAssistContracts::ExpectAction::Delay, 10,
     AssistReason::PassD0Safe},
    {"ideal6-limit4-best4", 4, 6, 4, false, 4, EbMissAssistContracts::ExpectAction::Delay, 4,
     EbMissAssistContracts::ExpectAction::Delay, 4, AssistReason::None},
    {"ideal6-limit4-d0-safe-best4", 4, 6, 4, true, 4, EbMissAssistContracts::ExpectAction::Pass, 0,
     EbMissAssistContracts::ExpectAction::Delay, 4, AssistReason::PassD0Safe},
    {"empty-corridor", 16, 0, 0, true, -1, EbMissAssistContracts::ExpectAction::Pass, 0,
     EbMissAssistContracts::ExpectAction::Pass, 0, AssistReason::PassNoThreat},
    {"spike-d0-dies-earliest8", 16, 8, 8, false, 8, EbMissAssistContracts::ExpectAction::Delay, 8,
     EbMissAssistContracts::ExpectAction::Delay, 8, AssistReason::None},
    {"hopeless-in-window", 8, 20, 0, false, -1, EbMissAssistContracts::ExpectAction::Pass, 0,
     EbMissAssistContracts::ExpectAction::Pass, 0, AssistReason::PassHopeless},
    {"ten-display-frames-outside-limit16", 16, 40, 0, true, -1,
     EbMissAssistContracts::ExpectAction::Pass, 0, EbMissAssistContracts::ExpectAction::Pass, 0,
     AssistReason::PassNoThreat},
    {"orb-activation-best12-d0-safe", 16, 12, 12, true, 12, EbMissAssistContracts::ExpectAction::Pass,
     0, EbMissAssistContracts::ExpectAction::Delay, 12, AssistReason::PassD0Safe},
};

int const EbMissAssistContractsImplementation::s_caseCount =
    static_cast<int>(sizeof(EbMissAssistContractsImplementation::s_cases) /
                     sizeof(EbMissAssistContractsImplementation::s_cases[0]));

int EbMissAssistContracts::caseCount() {
    return EbMissAssistContractsImplementation::s_caseCount;
}

EbMissAssistContracts::EarlyPressCase const& EbMissAssistContracts::caseAt(int index) {
    if (index < 0 || index >= EbMissAssistContractsImplementation::s_caseCount) {
        return EbMissAssistContractsImplementation::s_cases[0];
    }
    return EbMissAssistContractsImplementation::s_cases[index];
}

bool EbMissAssistContracts::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* label) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += ", ";
        report += label;
    };

    expect(EbMissAssistContracts::clampDelayToWindow(6, 4) == 4, "clamp-6-to-4");
    expect(EbMissAssistContracts::clampDelayToWindow(3, 4) == 3, "clamp-3-in-4");
    expect(EbMissAssistContracts::clampDelayToWindow(0, 4) == 0, "clamp-0");
    expect(EbMissAssistContracts::clampDelayToWindow(-2, 4) == 0, "clamp-neg");
    expect(EbMissAssistContracts::clampDelayToWindow(8, 0) == 0, "clamp-limit0");
    expect(EbMissAssistContracts::clampDelayToWindow(8, -1) == 0, "clamp-limit-neg");
    expect(EbMissAssistContracts::clampDelayToWindow(16, 16) == 16, "clamp-eq");

    expect(EbMissAssistContracts::fireDeadline(100, 16) == 116, "deadline-116");
    expect(EbMissAssistContracts::fireDeadline(0, 8) == 8, "deadline-8");
    expect(EbMissAssistContracts::fireDeadline(50, 0) == 50, "deadline-limit0");

    expect(EbMissAssistContracts::legacyExpect(true, 10) == ExpectAction::Pass, "legacy-d0-pass");
    expect(EbMissAssistContracts::legacyDelay(true, 10, 16) == 0, "legacy-d0-delay0");
    expect(EbMissAssistContracts::legacyExpect(false, 8) == ExpectAction::Delay,
           "legacy-earliest-delay");
    expect(EbMissAssistContracts::legacyDelay(false, 8, 16) == 8, "legacy-earliest-8");
    expect(EbMissAssistContracts::legacyDelay(false, 20, 16) == 16, "legacy-clamp-20-to-16");
    expect(EbMissAssistContracts::legacyExpect(false, -1) == ExpectAction::Pass,
           "legacy-hopeless-pass");

    expect(EbMissAssistContracts::newExpect(10, 16) == ExpectAction::Delay, "new-best10");
    expect(EbMissAssistContracts::newDelay(10, 16) == 10, "new-delay10");
    expect(EbMissAssistContracts::newExpect(0, 16) == ExpectAction::Pass, "new-best0-pass");
    expect(EbMissAssistContracts::newDelay(6, 4) == 4, "new-clamp-6-to-4");
    expect(EbMissAssistContracts::newExpect(6, 4) == ExpectAction::Delay, "new-clamped-still-delay");

    // Legacy-only helper: New pick never early-exits (wired in plan()).
    expect(EbMissAssistContracts::preferBandEarlyExitAllowed(false, true, true, true),
           "prefer-exit-no-orb-assist");
    expect(EbMissAssistContracts::preferBandEarlyExitAllowed(true, false, false, false),
           "prefer-exit-orb-idle");
    expect(!EbMissAssistContracts::preferBandEarlyExitAllowed(true, true, false, false),
           "prefer-exit-block-hint");
    expect(!EbMissAssistContracts::preferBandEarlyExitAllowed(true, false, true, false),
           "prefer-exit-block-spatial");
    expect(!EbMissAssistContracts::preferBandEarlyExitAllowed(true, false, false, true),
           "prefer-exit-block-foresight");

    expect(EbMissAssistContracts::caseCount() >= 6, "case-count");
    for (int index = 0; index < EbMissAssistContracts::caseCount(); ++index) {
        auto const& c = EbMissAssistContracts::caseAt(index);
        expect(c.name != nullptr && c.name[0] != '\0', "case-name");
        expect(c.limit >= 0, "case-limit");

        int const newD = EbMissAssistContracts::newDelay(c.bestInWindow, c.limit);
        expect(newD <= c.limit, "case-new-leq-limit");
        expect(newD == c.newDelayExpect, "case-new-delay");
        expect(EbMissAssistContracts::newExpect(c.bestInWindow, c.limit) == c.newAction,
               "case-new-action");

        int const legD = EbMissAssistContracts::legacyDelay(c.d0Viable, c.earliestViable, c.limit);
        expect(legD <= c.limit, "case-legacy-leq-limit");
        expect(legD == c.legacyDelayExpect, "case-legacy-delay");
        expect(EbMissAssistContracts::legacyExpect(c.d0Viable, c.earliestViable) == c.legacyAction,
               "case-legacy-action");

        expect(EbMissAssistContracts::fireDeadline(0, c.limit) == c.limit, "case-deadline");

        if (c.passReasonHint != AssistReason::None) {
            expect(EbAssistVerdict::isKnown(c.passReasonHint), "case-reason-known");
            if (c.legacyAction == ExpectAction::Pass || c.newAction == ExpectAction::Pass) {
                expect(EbAssistVerdict::isPass(c.passReasonHint), "case-reason-pass");
            }
        }
    }

    expect(EbMissAssistContracts::newDelay(6, 4) == 4 &&
               EbMissAssistContracts::legacyDelay(false, 6, 4) == 4,
           "g1-never-beyond-window");

    {
        auto const& c = EbMissAssistContracts::caseAt(0);
        expect(c.legacyAction == ExpectAction::Pass && c.newAction == ExpectAction::Delay &&
                   c.newDelayExpect == 10,
               "early10-new-vs-legacy");
    }

    if (fails == 0) {
        report = "MissAssistContracts selfCheck OK";
        return true;
    }
    report = std::string("MissAssistContracts selfCheck fails: ") + report;
    return false;
}
