#include "AssistRealMetrics.hpp"

#include "AssistContracts142.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

AssistRealMetrics& AssistRealMetrics::get() {
    static AssistRealMetrics inst;
    return inst;
}

int AssistRealMetrics::modeIndex(GameModeKind mode) {
    int const i = static_cast<int>(mode);
    if (i < 0 || i >= StageConfig::MODE_COUNT) return 0;
    return i;
}

AssistRealMetrics::ModeAcc& AssistRealMetrics::modeAccMut(GameModeKind mode) {
    return this->m_modes[static_cast<size_t>(modeIndex(mode))];
}

AssistRealMetrics::ModeAcc const& AssistRealMetrics::modeAcc(GameModeKind mode) const {
    return this->m_modes[static_cast<size_t>(modeIndex(mode))];
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
    this->m_modes.fill(ModeAcc{});
}

void AssistRealMetrics::setLevelKey(std::string key, bool dynamicLabelled) {
    this->m_levelKey = std::move(key);
    if (this->m_levelKey.empty()) return;
    auto& acc = this->m_levels[this->m_levelKey];
    if (dynamicLabelled) acc.dynamicLabelled = true;
}

void AssistRealMetrics::noteOpportunity(bool trusted, GameModeKind mode) {
    ++this->m_nOpp;
    if (trusted) ++this->m_nTrustOpp;
    auto& modeAcc = this->modeAccMut(mode);
    ++modeAcc.nOpp;
    if (trusted) ++modeAcc.nTrustOpp;
    if (!this->m_levelKey.empty()) {
        auto& acc = this->m_levels[this->m_levelKey];
        ++acc.nOpp;
        if (trusted) ++acc.nTrustOpp;
    }
}

void AssistRealMetrics::noteArm(GameModeKind mode) {
    ++this->m_arms;
    ++this->modeAccMut(mode).arms;
}

void AssistRealMetrics::noteDeathAfterFire(GameModeKind mode) {
    ++this->m_daf;
    ++this->modeAccMut(mode).daf;
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

float AssistRealMetrics::trustRateFor(GameModeKind mode) const {
    auto const& a = this->modeAcc(mode);
    int const d = std::max(1, a.nOpp);
    return static_cast<float>(a.nTrustOpp) / static_cast<float>(d);
}

float AssistRealMetrics::assistCovFor(GameModeKind mode) const {
    auto const& a = this->modeAcc(mode);
    int const d = std::max(1, a.nTrustOpp);
    return static_cast<float>(a.arms) / static_cast<float>(d);
}

float AssistRealMetrics::dafRateFor(GameModeKind mode) const {
    auto const& a = this->modeAcc(mode);
    int const d = std::max(1, a.arms);
    return static_cast<float>(a.daf) / static_cast<float>(d);
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

    auto const r1 = AssistContracts142::gateR1AssistCov(
        this->m_arms, this->m_nTrustOpp
    );
    auto const r2 = AssistContracts142::gateR2TrustRate(
        this->m_nTrustOpp, this->m_nOpp, false
    );
    auto const r3 = AssistContracts142::gateR3Daf(
        this->m_daf, this->m_arms, corpus
    );

    std::string modesBit;
    for (int i = 0; i < StageConfig::MODE_COUNT; ++i) {
        auto const kind = static_cast<GameModeKind>(i);
        auto const& a = this->m_modes[static_cast<size_t>(i)];
        if (a.nOpp <= 0 && a.arms <= 0) continue;
        if (!modesBit.empty()) modesBit += ' ';
        modesBit += fmt::format(
            "{}:A={}/opp={}/trust={}/daf={} cov={:.2f} tr={:.2f}",
            StageConfig::kindName(kind),
            a.arms,
            a.nOpp,
            a.nTrustOpp,
            a.daf,
            this->assistCovFor(kind),
            this->trustRateFor(kind)
        );
    }
    if (modesBit.empty()) modesBit = "none";

    return fmt::format(
        "corpus={} contracts142={} A={} DAF={} DAFRate={:.4f} CI95=[{:.4f},{:.4f}] "
        "ParityOK={}/{}={:.4f} AssistCov={:.4f} TrustRate={:.4f} "
        "N_opp={} N_trust={} max|dxy|=({:.1f},{:.1f}) "
        "G5={} G6={} G7a={} G7b={} R1={} R2={} R3={} sample={} modes=[{}]",
        corpus,
        AssistContracts142::freezeStatus(),
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
        AssistContracts140::statusId(r1),
        AssistContracts140::statusId(r2),
        AssistContracts140::statusId(r3),
        AssistContracts140::isSoloCorpus(corpus) ? "solo" : "multi",
        modesBit
    );
}

std::string AssistRealMetrics::overlayBit() const {
    if (this->m_arms <= 0 && this->m_parityProbes <= 0 && this->m_nOpp <= 0) {
        return {};
    }
    // Real metrics primary (DAF / AssistCov / TrustRate / Parity).
    return fmt::format(
        "daf={}/{} cov={:.0f}% trust={:.0f}% par={}/{}",
        this->m_daf,
        this->m_arms,
        this->assistCov() * 100.f,
        this->trustRate() * 100.f,
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
    expect(m.modeAcc(GameModeKind::Ship).arms == 0, "mode-fresh");

    m.setLevelKey("static-demo", false);
    m.noteOpportunity(true, GameModeKind::Cube);
    m.noteOpportunity(true, GameModeKind::Ship);
    m.noteOpportunity(false, GameModeKind::Ship);
    expect(m.nOpp() == 3 && m.nTrustOpp() == 2, "opp-count");
    expect(m.modeAcc(GameModeKind::Cube).nOpp == 1, "cube-opp");
    expect(m.modeAcc(GameModeKind::Ship).nOpp == 2, "ship-opp");
    expect(m.modeAcc(GameModeKind::Ship).nTrustOpp == 1, "ship-trust");

    m.noteArm(GameModeKind::Cube);
    m.noteArm(GameModeKind::Ship);
    expect(m.arms() == 2, "arm-count");
    expect(m.modeAcc(GameModeKind::Cube).arms == 1, "cube-arm");
    expect(m.modeAcc(GameModeKind::Ship).arms == 1, "ship-arm");
    expect(std::fabs(m.assistCov() - 1.f) < 1e-4f, "cov-1");
    expect(std::fabs(m.trustRate() - (2.f / 3.f)) < 1e-4f, "trust-rate");
    expect(std::fabs(m.assistCovFor(GameModeKind::Cube) - 1.f) < 1e-4f, "cube-cov");

    m.noteParityProbe(false, 1.f, 2.f);
    m.noteParityProbe(true, 20.f, 3.f);
    expect(m.parityProbes() == 2 && m.parityOk() == 1, "parity");
    expect(m.driftEvents() == 1, "drift");
    expect(m.maxAbsDx() >= 20.f && m.maxAbsDy() >= 3.f, "noise-max");

    m.noteDeathAfterFire(GameModeKind::Ship);
    expect(m.daf() == 1, "daf");
    expect(m.modeAcc(GameModeKind::Ship).daf == 1, "ship-daf");
    expect(m.modeAcc(GameModeKind::Cube).daf == 0, "cube-no-daf");

    auto const line = m.reportLine("solo");
    expect(line.find("corpus=solo") != std::string::npos, "line-corpus");
    expect(line.find("sample=solo") != std::string::npos, "line-sample");
    expect(line.find("contracts142=UNFROZEN") != std::string::npos, "line-142");
    expect(line.find("modes=[") != std::string::npos, "line-modes");
    expect(line.find("ship:") != std::string::npos, "line-ship");
    expect(line.find("simEstimate") == std::string::npos, "no-sim-estimate");

    // Default args keep Cube for call sites that omit mode.
    m.resetSession();
    m.noteOpportunity(true);
    m.noteArm();
    expect(m.modeAcc(GameModeKind::Cube).nOpp == 1, "default-mode-opp");
    expect(m.modeAcc(GameModeKind::Cube).arms == 1, "default-mode-arm");

    m.resetSession();
    expect(m.nOpp() == 0 && m.daf() == 0, "reset");
    expect(m.modeAcc(GameModeKind::Ship).arms == 0, "mode-reset");

    if (fails == 0) {
        report = "AssistRealMetrics selfCheck OK";
        return true;
    }
    report = fmt::format("AssistRealMetrics selfCheck {} fails: {}", fails, report);
    return false;
}
