#include "AssistVerdict.hpp"

char const* AssistVerdict::name(AssistReason reason) {
    switch (reason) {
        case AssistReason::None: return "none";
        case AssistReason::PassInvalidArgs: return "pass-invalid-args";
        case AssistReason::PassNoThreat: return "pass-no-threat";
        case AssistReason::PassWaveProximity: return "pass-wave-proximity";
        case AssistReason::PassWaveFutile: return "pass-wave-futile";
        case AssistReason::PassD0Safe: return "pass-d0-safe";
        case AssistReason::PassHopeless: return "pass-hopeless";
        case AssistReason::PassBudget: return "pass-budget";
        case AssistReason::PassSpam: return "pass-spam";
        case AssistReason::PassUntrust: return "pass-untrust";
        case AssistReason::PassLowTrust: return "pass-low-trust";
        case AssistReason::PassLimitZero: return "pass-limit-zero";
        case AssistReason::PassDebounce: return "pass-debounce";
        case AssistReason::PassAssistOff: return "pass-assist-off";
        case AssistReason::PassScope: return "pass-scope";
        case AssistReason::PassEarlyAttempt: return "pass-early-attempt";
        case AssistReason::PassCps: return "pass-cps";
        case AssistReason::PassBakeMissing: return "pass-bake-missing";
        case AssistReason::PassBakeNoWindow: return "pass-bake-no-window";
        case AssistReason::PassBakePastLatest: return "pass-bake-past-latest";
        case AssistReason::PassBakeVerifyFail: return "pass-bake-verify-fail";
        case AssistReason::PassBakeInWindow: return "pass-bake-in-window";
        case AssistReason::DelayOrb: return "delay-orb";
        case AssistReason::DelayThreat: return "delay-threat";
        case AssistReason::DelayPrefer: return "delay-prefer";
        case AssistReason::DelayGeneric: return "delay-generic";
        case AssistReason::HoldPending: return "hold-pending";
        case AssistReason::PassToggleUnknown: return "pass-toggle-unknown";
        case AssistReason::PassParityDrift: return "pass-parity-drift";
        case AssistReason::PassSpotUntrusted: return "pass-spot-untrusted";
        case AssistReason::PassUnknown: return "pass-unknown";
    }
    return "pass-unknown";
}

char const* AssistVerdict::overlayBit(AssistReason reason) {
    switch (reason) {
        case AssistReason::None: return "";
        case AssistReason::PassInvalidArgs: return "PASS invalid";
        case AssistReason::PassNoThreat: return "PASS no-threat";
        case AssistReason::PassWaveProximity: return "PASS wave-prox";
        case AssistReason::PassWaveFutile: return "PASS wave-futile";
        case AssistReason::PassD0Safe: return "PASS d0-safe";
        case AssistReason::PassHopeless: return "PASS hopeless";
        case AssistReason::PassBudget: return "PASS budget";
        case AssistReason::PassSpam: return "PASS spam";
        case AssistReason::PassUntrust: return "PASS untrust";
        case AssistReason::PassLowTrust: return "PASS low-trust";
        case AssistReason::PassLimitZero: return "PASS limit0";
        case AssistReason::PassDebounce: return "PASS debounce";
        case AssistReason::PassAssistOff: return "PASS off";
        case AssistReason::PassScope: return "PASS scope";
        case AssistReason::PassEarlyAttempt: return "PASS early";
        case AssistReason::PassCps: return "PASS cps";
        case AssistReason::PassBakeMissing: return "PASS bake-miss";
        case AssistReason::PassBakeNoWindow: return "PASS bake-none";
        case AssistReason::PassBakePastLatest: return "PASS bake-late";
        case AssistReason::PassBakeVerifyFail: return "PASS bake-verify";
        case AssistReason::PassBakeInWindow: return "PASS bake-in";
        case AssistReason::DelayOrb: return "DELAY orb";
        case AssistReason::DelayThreat: return "DELAY threat";
        case AssistReason::DelayPrefer: return "DELAY prefer";
        case AssistReason::DelayGeneric: return "DELAY";
        case AssistReason::HoldPending: return "HOLD~";
        case AssistReason::PassToggleUnknown: return "PASS toggle-unk";
        case AssistReason::PassParityDrift: return "PASS parity";
        case AssistReason::PassSpotUntrusted: return "PASS spot";
        case AssistReason::PassUnknown: return "PASS unknown";
    }
    return "PASS unknown";
}

bool AssistVerdict::isPass(AssistReason reason) {
    switch (reason) {
        case AssistReason::None:
        case AssistReason::DelayOrb:
        case AssistReason::DelayThreat:
        case AssistReason::DelayPrefer:
        case AssistReason::DelayGeneric:
        case AssistReason::HoldPending:
            return false;
        default:
            return true;
    }
}

bool AssistVerdict::isDelay(AssistReason reason) {
    switch (reason) {
        case AssistReason::DelayOrb:
        case AssistReason::DelayThreat:
        case AssistReason::DelayPrefer:
        case AssistReason::DelayGeneric:
            return true;
        default:
            return false;
    }
}

bool AssistVerdict::isHold(AssistReason reason) {
    return reason == AssistReason::HoldPending;
}

bool AssistVerdict::isKnown(AssistReason reason) {
    int const v = static_cast<int>(reason);
    return v > static_cast<int>(AssistReason::None)
        && v < static_cast<int>(AssistReason::PassUnknown);
}

AssistReason AssistVerdict::inferDelayReason(
    bool activatedRing, bool requireOrb, bool usedPreferBand
) {
    if (activatedRing || requireOrb) return AssistReason::DelayOrb;
    if (usedPreferBand) return AssistReason::DelayPrefer;
    return AssistReason::DelayThreat;
}

bool AssistVerdict::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* label) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += ", ";
        report += label;
    };

    // Every enum value must map to a non-empty stable name.
    int const first = static_cast<int>(AssistReason::None);
    int const last = static_cast<int>(AssistReason::PassUnknown);
    for (int i = first; i <= last; ++i) {
        auto const r = static_cast<AssistReason>(i);
        char const* n = AssistVerdict::name(r);
        expect(n != nullptr && n[0] != '\0', "name-empty");
        char const* o = AssistVerdict::overlayBit(r);
        expect(o != nullptr, "overlay-null");
        if (r == AssistReason::None) {
            expect(o[0] == '\0', "overlay-none-empty");
            expect(!AssistVerdict::isPass(r), "none-not-pass");
            expect(!AssistVerdict::isDelay(r), "none-not-delay");
            expect(!AssistVerdict::isKnown(r), "none-not-known");
        }
        else if (r == AssistReason::PassUnknown) {
            expect(AssistVerdict::isPass(r), "unknown-is-pass");
            expect(!AssistVerdict::isKnown(r), "unknown-not-known");
            expect(std::string(n) == "pass-unknown", "unknown-name");
        }
        else if (AssistVerdict::isDelay(r)) {
            expect(!AssistVerdict::isPass(r), "delay-not-pass");
            expect(AssistVerdict::isKnown(r), "delay-known");
            expect(std::string(AssistVerdict::overlayBit(r)).find("DELAY") == 0, "delay-overlay");
        }
        else if (AssistVerdict::isHold(r)) {
            expect(!AssistVerdict::isPass(r), "hold-not-pass");
            expect(AssistVerdict::isKnown(r), "hold-known");
        }
        else {
            expect(AssistVerdict::isPass(r), "pass-is-pass");
            expect(AssistVerdict::isKnown(r), "pass-known");
            expect(std::string(AssistVerdict::overlayBit(r)).find("PASS") == 0, "pass-overlay");
        }
    }

    // Out-of-range → pass-unknown name (defensive).
    expect(
        std::string(AssistVerdict::name(static_cast<AssistReason>(999))) == "pass-unknown",
        "oob-name"
    );

    expect(
        AssistVerdict::inferDelayReason(true, false, false) == AssistReason::DelayOrb,
        "infer-orb"
    );
    expect(
        AssistVerdict::inferDelayReason(false, true, false) == AssistReason::DelayOrb,
        "infer-require-orb"
    );
    expect(
        AssistVerdict::inferDelayReason(false, false, true) == AssistReason::DelayPrefer,
        "infer-prefer"
    );
    expect(
        AssistVerdict::inferDelayReason(false, false, false) == AssistReason::DelayThreat,
        "infer-threat"
    );

    // No two distinct known reasons share a name (except defensive unknown).
    for (int i = first; i <= last; ++i) {
        for (int j = i + 1; j <= last; ++j) {
            auto const a = static_cast<AssistReason>(i);
            auto const b = static_cast<AssistReason>(j);
            if (a == AssistReason::PassUnknown || b == AssistReason::PassUnknown) continue;
            if (a == AssistReason::None || b == AssistReason::None) continue;
            expect(
                std::string(AssistVerdict::name(a)) != std::string(AssistVerdict::name(b)),
                "name-unique"
            );
        }
    }

    if (fails == 0) {
        report = "AssistVerdict selfCheck OK";
        return true;
    }
    report = std::string("AssistVerdict selfCheck fails: ") + report;
    return false;
}
