#include "AssistContracts140.hpp"
#include "GhostParityProbe.hpp"

#include <cmath>
#include <cstring>
#include <fmt/format.h>

AssistContracts140::WilsonInterval AssistContracts140::wilson95(int x, int n) {
    WilsonInterval w;
    w.x = x < 0 ? 0 : x;
    w.n = n < 0 ? 0 : n;
    if (w.n <= 0) {
        w.phat = 0.f;
        w.lower = 0.f;
        w.upper = 1.f;
        return w;
    }
    if (w.x > w.n) w.x = w.n;
    double const nn = static_cast<double>(w.n);
    double const phat = static_cast<double>(w.x) / nn;
    double const z = 1.96;
    double const z2 = z * z;
    double const denom = 1.0 + z2 / nn;
    double const centre = phat + z2 / (2.0 * nn);
    double const rad = z * std::sqrt(
        (phat * (1.0 - phat) + z2 / (4.0 * nn)) / nn
    );
    w.phat = static_cast<float>(phat);
    w.lower = static_cast<float>(std::max(0.0, (centre - rad) / denom));
    w.upper = static_cast<float>(std::min(1.0, (centre + rad) / denom));
    return w;
}

bool AssistContracts140::isSoloCorpus(char const* corpusLabel) {
    return !corpusLabel || std::strcmp(corpusLabel, "multi") != 0;
}

int AssistContracts140::aMinForCorpus(char const* corpusLabel) {
    return AssistContracts140::isSoloCorpus(corpusLabel)
        ? AssistContracts140::kAMinSolo
        : AssistContracts140::kAMinMulti;
}

char const* AssistContracts140::statusId(GateStatus s) {
    switch (s) {
        case GateStatus::Pass: return "PASS";
        case GateStatus::Fail: return "FAIL";
        case GateStatus::Inconclusive: return "INCONCLUSIVE";
    }
    return "INCONCLUSIVE";
}

AssistContracts140::GateStatus AssistContracts140::gateG5Daf(
    int daf, int arms, char const* corpusLabel
) {
    int const amin = AssistContracts140::aMinForCorpus(corpusLabel);
    if (arms < amin) return GateStatus::Inconclusive;
    if (daf < 0) daf = 0;
    auto const ci = AssistContracts140::wilson95(daf, arms);
    if (ci.upper <= AssistContracts140::kG5DafCiUpperMax) {
        return GateStatus::Pass;
    }
    return GateStatus::Fail;
}

AssistContracts140::GateStatus AssistContracts140::gateG6Parity(
    int okProbes, int totalProbes
) {
    if (totalProbes <= 0) return GateStatus::Inconclusive;
    if (okProbes < 0) okProbes = 0;
    if (okProbes > totalProbes) okProbes = totalProbes;
    float const rate = static_cast<float>(okProbes)
        / static_cast<float>(totalProbes);
    if (rate >= AssistContracts140::kG6ParityOkMin) return GateStatus::Pass;
    return GateStatus::Fail;
}

AssistContracts140::GateStatus AssistContracts140::gateG7aAssistCov(
    int arms, int trustOpp
) {
    if (trustOpp <= 0) return GateStatus::Inconclusive;
    if (arms < 0) arms = 0;
    float const cov = static_cast<float>(arms)
        / static_cast<float>(trustOpp);
    if (cov >= AssistContracts140::kG7aAssistCovMin) return GateStatus::Pass;
    return GateStatus::Fail;
}

AssistContracts140::GateStatus AssistContracts140::gateG7bTrustRate(
    int trustOpp, int opp, bool dynamicLabelled
) {
    if (opp < AssistContracts140::kOppMinPerLevel) {
        return GateStatus::Inconclusive;
    }
    if (trustOpp < 0) trustOpp = 0;
    float const rate = static_cast<float>(trustOpp)
        / static_cast<float>(std::max(1, opp));
    float const need = dynamicLabelled
        ? AssistContracts140::kG7bTrustRateDynamicMin
        : AssistContracts140::kG7bTrustRateStaticMin;
    if (rate >= need) return GateStatus::Pass;
    return GateStatus::Fail;
}

void AssistContracts140::applyLockedEps() {
    GhostParityProbe::get().setEps(
        AssistContracts140::kLockedEpsX,
        AssistContracts140::kLockedEpsY
    );
}

bool AssistContracts140::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    // Wilson: 0/100 → upper still small; 2/100 → upper around a few %.
    auto const z0 = AssistContracts140::wilson95(0, 100);
    expect(z0.phat == 0.f, "wilson-0-phat");
    expect(z0.upper > 0.f && z0.upper < 0.05f, "wilson-0-upper-small");

    auto const z2 = AssistContracts140::wilson95(2, 100);
    expect(z2.phat > 0.01f && z2.phat < 0.03f, "wilson-2-phat");
    expect(z2.upper > z2.phat, "wilson-2-upper-gt");

    expect(
        AssistContracts140::gateG5Daf(0, 30, "solo")
            == GateStatus::Inconclusive,
        "g5-solo-under-amin"
    );
    expect(
        AssistContracts140::gateG5Daf(0, 40, "solo") == GateStatus::Pass,
        "g5-solo-zero-daf"
    );
    expect(
        AssistContracts140::gateG5Daf(5, 40, "solo") == GateStatus::Fail,
        "g5-solo-high-daf"
    );
    expect(
        AssistContracts140::gateG5Daf(0, 99, "multi")
            == GateStatus::Inconclusive,
        "g5-multi-under-amin"
    );

    expect(
        AssistContracts140::gateG6Parity(99, 100) == GateStatus::Pass,
        "g6-99"
    );
    expect(
        AssistContracts140::gateG6Parity(98, 100) == GateStatus::Fail,
        "g6-98"
    );
    expect(
        AssistContracts140::gateG6Parity(0, 0) == GateStatus::Inconclusive,
        "g6-empty"
    );

    expect(
        AssistContracts140::gateG7aAssistCov(95, 100) == GateStatus::Pass,
        "g7a-95"
    );
    expect(
        AssistContracts140::gateG7aAssistCov(94, 100) == GateStatus::Fail,
        "g7a-94"
    );

    expect(
        AssistContracts140::gateG7bTrustRate(14, 20, false)
            == GateStatus::Pass,
        "g7b-static-70"
    );
    expect(
        AssistContracts140::gateG7bTrustRate(13, 20, false)
            == GateStatus::Fail,
        "g7b-static-low"
    );
    expect(
        AssistContracts140::gateG7bTrustRate(2, 20, true) == GateStatus::Pass,
        "g7b-dyn-10"
    );
    expect(
        AssistContracts140::gateG7bTrustRate(1, 20, true) == GateStatus::Fail,
        "g7b-dyn-low"
    );
    expect(
        AssistContracts140::gateG7bTrustRate(20, 19, false)
            == GateStatus::Inconclusive,
        "g7b-opp-min"
    );

    expect(
        AssistContracts140::kLockedEpsX == GhostParityProbe::kDefaultEpsX,
        "eps-lock-matches-default-x"
    );
    expect(
        AssistContracts140::kLockedEpsY == GhostParityProbe::kDefaultEpsY,
        "eps-lock-matches-default-y"
    );
    AssistContracts140::applyLockedEps();
    expect(
        GhostParityProbe::get().epsX() == AssistContracts140::kLockedEpsX,
        "apply-eps-x"
    );
    expect(
        GhostParityProbe::get().epsY() == AssistContracts140::kLockedEpsY,
        "apply-eps-y"
    );

    expect(std::strcmp(statusId(GateStatus::Pass), "PASS") == 0, "id-pass");
    expect(std::strcmp(kCorpusLabel, "solo") == 0, "corpus-solo-default");

    if (fails == 0) {
        report = "AssistContracts140 selfCheck OK";
        return true;
    }
    report = fmt::format("AssistContracts140 selfCheck {} fails: {}", fails, report);
    return false;
}
