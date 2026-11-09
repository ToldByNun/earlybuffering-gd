#include "perfect_assist_policy.hpp"
#include <cmath>
#include <fmt/format.h>

bool EbPerfectAssistPolicy::isOpportunity(OpportunityInput const& o) {
    return o.foresightOrbPad || o.threatZoneValid || o.idleSafer || o.ascentUseful ||
           o.bakePreferBand || o.orbWindowFound || o.d0ActivatedRing || o.probeShowsGain;
}

bool EbPerfectAssistPolicy::trustOk(TrustInput const& t) {
    return !t.parityDrift && !t.spotUntrusted && !t.divergenceUntrusted && !t.twEmptyWhenExpected &&
           !t.hardUnknownHit;
}

bool EbPerfectAssistPolicy::forcePassReason(TrustInput const& t, AssistReason& out) {
    if (t.parityDrift) {
        out = AssistReason::PassParityDrift;
        return true;
    }
    if (t.spotUntrusted) {
        out = AssistReason::PassSpotUntrusted;
        return true;
    }
    if (t.twEmptyWhenExpected) {
        out = AssistReason::PassTwEmpty;
        return true;
    }
    if (t.hardUnknownHit) {
        out = AssistReason::PassHardUnknown;
        return true;
    }
    if (t.divergenceUntrusted) {
        out = AssistReason::PassUntrust;
        return true;
    }
    out = AssistReason::None;
    return false;
}

EbPerfectAssistPolicy::Decision EbPerfectAssistPolicy::classify(TrustInput const& t,
                                                            OpportunityInput const& o) {
    AssistReason ignored = AssistReason::None;
    if (forcePassReason(t, ignored))
        return Decision::Pass;
    if (!(isOpportunity(o) && trustOk(t))) {
        return Decision::AllowVanillaNoThreat;
    }
    return Decision::MustDelaySearch;
}

bool EbPerfectAssistPolicy::allowPassBudget(bool trustOkFlag, bool opportunity,
                                          bool budgetRemainsForProbe) {
    // Trusted opportunity with remaining probe budget must keep searching.
    if (!(trustOkFlag && opportunity && budgetRemainsForProbe)) {
        return true;
    }
    return false;
}

bool EbPerfectAssistPolicy::allowArmDelay(bool candViable, bool budgetHit, bool softTrustPartial) {
    // Only block inventing a Delay from an incomplete partial pick under budgetHit.
    if (!(softTrustPartial && budgetHit)) {
        return candViable;
    }
    return false;
}

bool EbPerfectAssistPolicy::allowInventClick() {
    return false;
}

bool EbPerfectAssistPolicy::allowInventRelease() {
    return false;
}

bool EbPerfectAssistPolicy::interveneOk(bool d0Viable, bool delayViable, bool delayClearsBetter) {
    if (!delayViable)
        return false;
    if (!(d0Viable && !delayClearsBetter)) {
        return true;
    }
    return false;
}

bool EbPerfectAssistPolicy::preferSmallerWaveClear(float clearA, float clearB, float marginPx) {
    bool const aOk = clearA >= marginPx + 1.f;
    bool const bOk = clearB >= marginPx + 1.f;
    if (aOk != bOk)
        return aOk;
    if (!(!aOk)) {
        return clearA <= clearB;
    }
    return false;
}

bool EbPerfectAssistPolicy::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += "; ";
        report += name;
    };

    TrustInput badDrift{};
    badDrift.parityDrift = true;
    AssistReason result = AssistReason::None;
    expect(forcePassReason(badDrift, result) && result == AssistReason::PassParityDrift,
           "pass-drift");
    expect(!trustOk(badDrift), "trust-drift");

    TrustInput badSpot{};
    badSpot.spotUntrusted = true;
    expect(forcePassReason(badSpot, result) && result == AssistReason::PassSpotUntrusted,
           "pass-spot");

    TrustInput badTw{};
    badTw.twEmptyWhenExpected = true;
    expect(forcePassReason(badTw, result) && result == AssistReason::PassTwEmpty, "pass-tw");

    TrustInput badHard{};
    badHard.hardUnknownHit = true;
    expect(forcePassReason(badHard, result) && result == AssistReason::PassHardUnknown,
           "pass-hard");

    TrustInput badUn{};
    badUn.divergenceUntrusted = true;
    expect(forcePassReason(badUn, result) && result == AssistReason::PassUntrust, "pass-untrust");

    TrustInput ok{};
    expect(trustOk(ok), "trust-ok");
    expect(!forcePassReason(ok, result), "no-force");

    OpportunityInput empty{};
    expect(!isOpportunity(empty), "no-opp");
    OpportunityInput orb{};
    orb.foresightOrbPad = true;
    expect(isOpportunity(orb), "opp-orb");

    expect(classify(ok, empty) == Decision::AllowVanillaNoThreat, "vanilla-no-threat");
    expect(classify(ok, orb) == Decision::MustDelaySearch, "must-search");
    expect(classify(badDrift, orb) == Decision::Pass, "pass-over-opp");

    expect(!allowPassBudget(true, true, true), "budget-block");
    expect(allowPassBudget(true, true, false), "budget-empty-ok");
    expect(allowPassBudget(false, true, true), "budget-untrust-ok");

    expect(allowArmDelay(true, false), "arm-ok");
    expect(allowArmDelay(true, true, false), "arm-ok-budget-exhausted-search");
    expect(!allowArmDelay(true, true, true), "arm-no-soft-partial");
    expect(!allowArmDelay(false, false), "arm-not-viable");

    expect(!allowInventClick(), "no-invent-click");
    expect(!allowInventRelease(), "no-invent-release");

    expect(interveneOk(false, true, true), "intervene-save");
    expect(!interveneOk(true, true, false), "intervene-no-hurt");
    expect(!interveneOk(false, false, true), "intervene-not-viable");
    // Callers must not pass ascentUseful alone as delayClearsBetter when d0 Alive.
    expect(interveneOk(true, true, true), "intervene-real-improve");
    expect(!interveneOk(true, true, false), "intervene-ascent-flag-not-enough");

    expect(preferSmallerWaveClear(3.f, 8.f, 1.f), "wave-smaller");
    expect(!preferSmallerWaveClear(8.f, 3.f, 1.f), "wave-not-larger");
    expect(!preferSmallerWaveClear(0.5f, 0.5f, 1.f), "wave-under-margin");

    if (fails == 0) {
        report = "PerfectAssistPolicy selfCheck OK";
        return true;
    }
    report = fmt::format("PerfectAssistPolicy selfCheck {} fails: {}", fails, report);
    return false;
}
