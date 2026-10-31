#include "SimTrust.hpp"
#include "SpotTrustMap.hpp"

#include <fmt/format.h>

SimTrust& SimTrust::get() {
    static SimTrust inst;
    return inst;
}

void SimTrust::resetAttempt() {
    this->m_parityDrift = false;
}

void SimTrust::onLevelQuit() {
    this->m_parityDrift = false;
    SpotTrustMap::get().clear();
}

void SimTrust::noteParityDrift() {
    this->m_parityDrift = true;
}

void SimTrust::clearParityDrift() {
    this->m_parityDrift = false;
}

bool SimTrust::okAt(float x) const {
    if (this->m_parityDrift) return false;
    if (SpotTrustMap::get().isUntrusted(x)) return false;
    return true;
}

bool SimTrust::forcePassReason(float x, AssistReason& outReason) const {
    if (this->m_parityDrift) {
        outReason = AssistReason::PassParityDrift;
        return true;
    }
    if (SpotTrustMap::get().isUntrusted(x)) {
        outReason = AssistReason::PassSpotUntrusted;
        return true;
    }
    return false;
}

char const* SimTrust::overlayBit() const {
    if (this->m_parityDrift) return "trust=0 parity";
    if (SpotTrustMap::get().untrustedCount() > 0) return "trust=spot";
    return "trust=1";
}

bool SimTrust::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    SpotTrustMap::get().clear();
    SimTrust& trust = SimTrust::get();
    trust.resetAttempt();
    expect(trust.okAt(0.f), "fresh-ok");
    AssistReason reason = AssistReason::None;
    expect(!trust.forcePassReason(0.f, reason), "fresh-no-force");

    trust.noteParityDrift();
    expect(!trust.okAt(0.f), "drift-blocks");
    expect(trust.forcePassReason(0.f, reason), "drift-force");
    expect(reason == AssistReason::PassParityDrift, "drift-reason");

    trust.clearParityDrift();
    SpotTrustMap::get().markUntrusted(200.f);
    expect(!trust.okAt(200.f), "spot-blocks");
    expect(trust.forcePassReason(200.f, reason), "spot-force");
    expect(reason == AssistReason::PassSpotUntrusted, "spot-reason");
    expect(trust.okAt(0.f), "other-x-ok");

    trust.onLevelQuit();
    expect(trust.okAt(200.f), "quit-clears");

    if (fails == 0) {
        report = "SimTrust selfCheck OK";
        return true;
    }
    report = fmt::format("SimTrust selfCheck {} fails: {}", fails, report);
    return false;
}
