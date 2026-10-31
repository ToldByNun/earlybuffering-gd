#include "AssistRealMetrics.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

AssistRealMetrics& AssistRealMetrics::get() {
    static AssistRealMetrics inst;
    return inst;
}

void AssistRealMetrics::resetSession() {
    this->m_nOpp = 0;
    this->m_nTrustOpp = 0;
    this->m_arms = 0;
    this->m_daf = 0;
    this->m_parityProbes = 0;
    this->m_parityOk = 0;
    this->m_driftEvents = 0;
    this->m_maxAbsDx = 0.f;
    this->m_maxAbsDy = 0.f;
    this->m_levelKey.clear();
    this->m_levels.clear();
}

void AssistRealMetrics::setLevelKey(std::string key, bool dynamicLabelled) {
    this->m_levelKey = std::move(key);
    if (this->m_levelKey.empty()) return;
    auto& acc = this->m_levels[this->m_levelKey];
    if (dynamicLabelled) acc.dynamicLabelled = true;
}

void AssistRealMetrics::noteOpportunity(bool trusted) {
    ++this->m_nOpp;
    if (trusted) ++this->m_nTrustOpp;
    if (!this->m_levelKey.empty()) {
        auto& acc = this->m_levels[this->m_levelKey];
        ++acc.nOpp;
        if (trusted) ++acc.nTrustOpp;
    }
}

void AssistRealMetrics::noteArm() {
    ++this->m_arms;
}

void AssistRealMetrics::noteDeathAfterFire() {
    ++this->m_daf;
}

void AssistRealMetrics::noteParityProbe(bool drifted, float absDx, float absDy) {
    ++this->m_parityProbes;
    if (!drifted) ++this->m_parityOk;
    else ++this->m_driftEvents;
    if (std::isfinite(absDx)) {
        this->m_maxAbsDx = std::max(this->m_maxAbsDx, absDx);
    }
    if (std::isfinite(absDy)) {
        this->m_maxAbsDy = std::max(this->m_maxAbsDy, absDy);
    }
}

float AssistRealMetrics::trustRate() const {
    int const d = std::max(1, this->m_nOpp);
    return static_cast<float>(this->m_nTrustOpp) / static_cast<float>(d);
}

float AssistRealMetrics::assistCov() const {
    int const d = std::max(1, this->m_nTrustOpp);
    return static_cast<float>(this->m_arms) / static_cast<float>(d);
}

float AssistRealMetrics::dafRate() const {
    int const d = std::max(1, this->m_arms);
    return static_cast<float>(this->m_daf) / static_cast<float>(d);
}

float AssistRealMetrics::parityOkRate() const {
    int const d = std::max(1, this->m_parityProbes);
    return static_cast<float>(this->m_parityOk) / static_cast<float>(d);
}

std::string AssistRealMetrics::reportLine(char const* corpusLabel) const {
    char const* corpus = corpusLabel && corpusLabel[0] != '\0'
        ? corpusLabel
        : AssistContracts140::kCorpusLabel;

    auto const ci = AssistContracts140::wilson95(this->m_daf, this->m_arms);
    auto const g5 = AssistContracts140::gateG5Daf(
        this->m_daf, this->m_arms, corpus
    );
    auto const g6 = AssistContracts140::gateG6Parity(
        this->m_parityOk, this->m_parityProbes
    );
    auto const g7a = AssistContracts140::gateG7aAssistCov(
        this->m_arms, this->m_nTrustOpp
    );

    // Per-level G7b: worst status among levels with enough opp (Fail > Inconclusive > Pass).
    auto worst = AssistContracts140::GateStatus::Pass;
    int levelsChecked = 0;
    for (auto const& kv : this->m_levels) {
        auto const st = AssistContracts140::gateG7bTrustRate(
            kv.second.nTrustOpp, kv.second.nOpp, kv.second.dynamicLabelled
        );
        ++levelsChecked;
        if (st == AssistContracts140::GateStatus::Fail) {
            worst = st;
            break;
        }
        if (st == AssistContracts140::GateStatus::Inconclusive
            && worst == AssistContracts140::GateStatus::Pass) {
            worst = st;
        }
    }
    if (levelsChecked == 0) {
        worst = AssistContracts140::gateG7bTrustRate(
            this->m_nTrustOpp, this->m_nOpp, false
        );
    }

    return fmt::format(
        "corpus={} A={} DAF={} DAFRate={:.4f} CI95=[{:.4f},{:.4f}] "
        "ParityOK={}/{}={:.4f} AssistCov={:.4f} TrustRate={:.4f} "
        "N_opp={} N_trust={} max|dxy|=({:.1f},{:.1f}) "
        "G5={} G6={} G7a={} G7b={} sample={}",
        corpus,
        this->m_arms,
        this->m_daf,
        this->dafRate(),
        ci.lower,
        ci.upper,
        this->m_parityOk,
        this->m_parityProbes,
        this->parityOkRate(),
        this->assistCov(),
        this->trustRate(),
        this->m_nOpp,
        this->m_nTrustOpp,
        this->m_maxAbsDx,
        this->m_maxAbsDy,
        AssistContracts140::statusId(g5),
        AssistContracts140::statusId(g6),
        AssistContracts140::statusId(g7a),
        AssistContracts140::statusId(worst),
        AssistContracts140::isSoloCorpus(corpus) ? "solo" : "multi"
    );
}

std::string AssistRealMetrics::overlayBit() const {
    if (this->m_arms <= 0 && this->m_parityProbes <= 0) return {};
    return fmt::format(
        "daf={}/{} par={}/{}",
        this->m_daf,
        this->m_arms,
        this->m_parityOk,
        this->m_parityProbes
    );
}

bool AssistRealMetrics::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    AssistRealMetrics& m = AssistRealMetrics::get();
    m.resetSession();
    expect(m.nOpp() == 0 && m.arms() == 0, "fresh");

    m.setLevelKey("static-demo", false);
    m.noteOpportunity(true);
    m.noteOpportunity(true);
    m.noteOpportunity(false);
    expect(m.nOpp() == 3 && m.nTrustOpp() == 2, "opp-count");
    m.noteArm();
    m.noteArm();
    expect(m.arms() == 2, "arm-count");
    expect(std::fabs(m.assistCov() - 1.f) < 1e-4f, "cov-1");
    expect(std::fabs(m.trustRate() - (2.f / 3.f)) < 1e-4f, "trust-rate");

    m.noteParityProbe(false, 1.f, 2.f);
    m.noteParityProbe(true, 20.f, 3.f);
    expect(m.parityProbes() == 2 && m.parityOk() == 1, "parity");
    expect(m.driftEvents() == 1, "drift");
    expect(m.maxAbsDx() >= 20.f && m.maxAbsDy() >= 3.f, "noise-max");

    m.noteDeathAfterFire();
    expect(m.daf() == 1, "daf");

    auto const line = m.reportLine("solo");
    expect(line.find("corpus=solo") != std::string::npos, "line-corpus");
    expect(line.find("sample=solo") != std::string::npos, "line-sample");
    expect(line.find("simEstimate") == std::string::npos, "no-sim-estimate");

    m.resetSession();
    expect(m.nOpp() == 0 && m.daf() == 0, "reset");

    if (fails == 0) {
        report = "AssistRealMetrics selfCheck OK";
        return true;
    }
    report = fmt::format("AssistRealMetrics selfCheck {} fails: {}", fails, report);
    return false;
}
