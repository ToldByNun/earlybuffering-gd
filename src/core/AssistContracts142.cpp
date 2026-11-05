#include "AssistContracts142.hpp"

#include <fmt/format.h>
#include <string>

char const* AssistContracts142::gateIdName(GateId id) {
    switch (id) {
    case GateId::R1AssistCov: return "R1-AssistCov";
    case GateId::R2TrustRate: return "R2-TrustRate";
    case GateId::R3Daf: return "R3-DAF";
    case GateId::R4RouteDirection: return "R4-RouteDirection";
    case GateId::R5OffRouteFallback: return "R5-OffRouteFallback";
    case GateId::R6PathStale: return "R6-PathStale";
    case GateId::R7PlanP95: return "R7-PlanP95";
    case GateId::R8CacheLoadP95: return "R8-CacheLoadP95";
    case GateId::Count:
    default: return "R?-unknown";
    }
}

char const* AssistContracts142::freezeStatus() {
    return kUnfrozen ? "UNFROZEN" : "LOCKED";
}

int AssistContracts142::aMinForCorpus(char const* corpusLabel) {
    return AssistContracts140::isSoloCorpus(corpusLabel) ? kAMinSolo : kAMinMulti;
}

int AssistContracts142::aMinPerMode(char const* corpusLabel) {
    (void)corpusLabel;
    return kAMinPerModeSolo;
}

AssistContracts140::GateStatus AssistContracts142::gateReportOnly() {
    return AssistContracts140::GateStatus::Inconclusive;
}

AssistContracts140::GateStatus AssistContracts142::gateR1AssistCov(
    int arms, int trustOpp
) {
    if (kUnfrozen) return AssistContracts140::GateStatus::Inconclusive;
    return AssistContracts140::gateG7aAssistCov(arms, trustOpp);
}

AssistContracts140::GateStatus AssistContracts142::gateR2TrustRate(
    int trustOpp, int opp, bool dynamicLabelled
) {
    if (kUnfrozen) return AssistContracts140::GateStatus::Inconclusive;
    return AssistContracts140::gateG7bTrustRate(trustOpp, opp, dynamicLabelled);
}

AssistContracts140::GateStatus AssistContracts142::gateR3Daf(
    int daf, int arms, char const* corpusLabel
) {
    if (kUnfrozen) return AssistContracts140::GateStatus::Inconclusive;
    return AssistContracts140::gateG5Daf(daf, arms, corpusLabel);
}

bool AssistContracts142::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    expect(kSchema == 1, "schema");
    expect(kUnfrozen, "unfrozen-stub");
    expect(std::string(freezeStatus()) == "UNFROZEN", "freeze-status");
    expect(
        gateR1AssistCov(100, 100) == AssistContracts140::GateStatus::Inconclusive,
        "r1-inconclusive"
    );
    expect(
        gateR2TrustRate(100, 100, false)
            == AssistContracts140::GateStatus::Inconclusive,
        "r2-inconclusive"
    );
    expect(
        gateR3Daf(0, 100, "solo")
            == AssistContracts140::GateStatus::Inconclusive,
        "r3-inconclusive"
    );
    expect(
        gateReportOnly() == AssistContracts140::GateStatus::Inconclusive,
        "report-only"
    );
    expect(
        std::string(gateIdName(GateId::R4RouteDirection)) == "R4-RouteDirection",
        "r4-name"
    );
    expect(aMinForCorpus("solo") == kAMinSolo, "amin-solo");
    expect(aMinPerMode("solo") == kAMinPerModeSolo, "amin-mode");
    expect(
        AssistContracts140::kLockedEpsX == 12.f
            && AssistContracts140::kLockedEpsY == 12.f,
        "eps-unchanged"
    );

    if (fails == 0) {
        report = "AssistContracts142 selfCheck OK";
        return true;
    }
    report = fmt::format("AssistContracts142 selfCheck {} fails: {}", fails, report);
    return false;
}
