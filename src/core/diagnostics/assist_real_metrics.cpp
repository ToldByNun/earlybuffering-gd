#include "../../app/class_manager.hpp"
#include "assist_real_metrics.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

EbAssistRealMetrics& EbAssistRealMetrics::get() {
    return EbClassManager::get().service<EbAssistRealMetrics>();
}

void EbAssistRealMetrics::resetSession() {
    this->m_nOpp = 0;
    this->m_nTrustOpp = 0;
    this->m_arms = 0;
    this->m_daf = 0;
    this->m_parityProbes = 0;
    this->m_parityOk = 0;
    this->m_driftEvents = 0;
    this->m_liveHeld = 0;
    this->m_liveAsMade = 0;
    this->m_livePassThrough = 0;
    this->m_liveWindowLegal = 0;
    this->m_liveWindowIllegal = 0;
    this->m_livePassBudget = 0;
    this->m_livePassUnknown = 0;
    this->m_livePassUntrust = 0;
    this->m_liveFalseMutation = 0;
    this->m_maxAbsDx = 0.f;
    this->m_maxAbsDy = 0.f;
    this->m_levelKey.clear();
    this->m_levels.clear();
}

void EbAssistRealMetrics::setLevelKey(std::string key, bool dynamicLabelled) {
    this->m_levelKey = std::move(key);
    if (this->m_levelKey.empty())
        return;
    auto& acc = this->m_levels[this->m_levelKey];
    if (dynamicLabelled)
        acc.dynamicLabelled = true;
}

void EbAssistRealMetrics::noteOpportunity(bool trusted) {
    ++this->m_nOpp;
    if (trusted)
        ++this->m_nTrustOpp;
    if (!this->m_levelKey.empty()) {
        auto& acc = this->m_levels[this->m_levelKey];
        ++acc.nOpp;
        if (trusted)
            ++acc.nTrustOpp;
    }
}

void EbAssistRealMetrics::noteArm() {
    ++this->m_arms;
}

void EbAssistRealMetrics::noteDeathAfterFire() {
    ++this->m_daf;
}

void EbAssistRealMetrics::noteLiveDecision(EbLiveStageParity::DecisionKind kind, bool windowOk) {
    switch (kind) {
    case EbLiveStageParity::DecisionKind::Held:
        ++this->m_liveHeld;
        if (windowOk)
            ++this->m_liveWindowLegal;
        else
            ++this->m_liveWindowIllegal;
        break;
    case EbLiveStageParity::DecisionKind::AsMade:
        ++this->m_liveAsMade;
        break;
    case EbLiveStageParity::DecisionKind::PassThrough:
        ++this->m_livePassThrough;
        break;
    }
}

void EbAssistRealMetrics::noteLivePassHonesty(AssistReason reason) {
    switch (reason) {
    case AssistReason::PassBudget:
        ++this->m_livePassBudget;
        break;
    case AssistReason::PassUnknown:
    case AssistReason::PassToggleUnknown:
    case AssistReason::PassHardUnknown:
        ++this->m_livePassUnknown;
        break;
    case AssistReason::PassUntrust:
    case AssistReason::PassLowTrust:
    case AssistReason::PassParityDrift:
    case AssistReason::PassSpotUntrusted:
        ++this->m_livePassUntrust;
        break;
    default:
        break;
    }
}

void EbAssistRealMetrics::noteLiveFalseMutation() {
    ++this->m_liveFalseMutation;
}

void EbAssistRealMetrics::noteParityProbe(bool drifted, float absDx, float absDy) {
    ++this->m_parityProbes;
    if (!drifted)
        ++this->m_parityOk;
    else
        ++this->m_driftEvents;
    if (std::isfinite(absDx)) {
        this->m_maxAbsDx = std::max(this->m_maxAbsDx, absDx);
    }
    if (std::isfinite(absDy)) {
        this->m_maxAbsDy = std::max(this->m_maxAbsDy, absDy);
    }
}

float EbAssistRealMetrics::trustRate() const {
    int const delay = std::max(1, this->m_nOpp);
    return static_cast<float>(this->m_nTrustOpp) / static_cast<float>(delay);
}

float EbAssistRealMetrics::assistCov() const {
    int const delay = std::max(1, this->m_nTrustOpp);
    return static_cast<float>(this->m_arms) / static_cast<float>(delay);
}

float EbAssistRealMetrics::dafRate() const {
    int const delay = std::max(1, this->m_arms);
    return static_cast<float>(this->m_daf) / static_cast<float>(delay);
}

float EbAssistRealMetrics::parityOkRate() const {
    int const delay = std::max(1, this->m_parityProbes);
    return static_cast<float>(this->m_parityOk) / static_cast<float>(delay);
}

float EbAssistRealMetrics::liveRescueRate() const {
    // Rescue = held Delay inside a legal window / trusted opportunities.
    int const denom = std::max(1, this->m_nTrustOpp);
    return static_cast<float>(this->m_liveWindowLegal) / static_cast<float>(denom);
}

float EbAssistRealMetrics::liveFalseMutationRate() const {
    int const denom = std::max(1, this->m_arms);
    return static_cast<float>(this->m_liveFalseMutation) / static_cast<float>(denom);
}

float EbAssistRealMetrics::livePassHonestyRate() const {
    int const passes =
        this->m_liveAsMade + this->m_livePassThrough + this->m_livePassBudget +
        this->m_livePassUnknown + this->m_livePassUntrust;
    int const honest =
        this->m_livePassBudget + this->m_livePassUnknown + this->m_livePassUntrust;
    int const denom = std::max(1, passes);
    // Fraction of Pass outcomes that carry an honest named bucket (not silent).
    return static_cast<float>(honest) / static_cast<float>(denom);
}

float EbAssistRealMetrics::liveWindowLegalityRate() const {
    int const total = this->m_liveWindowLegal + this->m_liveWindowIllegal;
    int const denom = std::max(1, total);
    return static_cast<float>(this->m_liveWindowLegal) / static_cast<float>(denom);
}

std::string EbAssistRealMetrics::reportLine(char const* corpusLabel) const {
    char const* corpus =
        corpusLabel && corpusLabel[0] != '\0' ? corpusLabel : EbAssistContracts140::kCorpusLabel;

    auto const ci = EbAssistContracts140::wilson95(this->m_daf, this->m_arms);
    auto const g5 = EbAssistContracts140::gateG5Daf(this->m_daf, this->m_arms, corpus);
    auto const g6 = EbAssistContracts140::gateG6Parity(this->m_parityOk, this->m_parityProbes);
    auto const g7a = EbAssistContracts140::gateG7aAssistCov(this->m_arms, this->m_nTrustOpp);

    // Per-level G7b: worst status among levels with enough opp (Fail > Inconclusive > Pass).
    auto worst = EbAssistContracts140::GateStatus::Pass;
    int levelsChecked = 0;
    for (auto const& kv : this->m_levels) {
        auto const state = EbAssistContracts140::gateG7bTrustRate(kv.second.nTrustOpp, kv.second.nOpp,
                                                                kv.second.dynamicLabelled);
        ++levelsChecked;
        if (state == EbAssistContracts140::GateStatus::Fail) {
            worst = state;
            break;
        }
        if (state == EbAssistContracts140::GateStatus::Inconclusive &&
            worst == EbAssistContracts140::GateStatus::Pass) {
            worst = state;
        }
    }
    if (levelsChecked == 0) {
        worst = EbAssistContracts140::gateG7bTrustRate(this->m_nTrustOpp, this->m_nOpp, false);
    }
    return fmt::format("corpus={} A={} DAF={} DAFRate={:.4f} CI95=[{:.4f},{:.4f}] "
                       "ParityOK={}/{}={:.4f} AssistCov={:.4f} TrustRate={:.4f} "
                       "N_opp={} N_trust={} max|dxy|=({:.1f},{:.1f}) "
                       "G5={} G6={} G7a={} G7b={} sample={}",
                       corpus, this->m_arms, this->m_daf, this->dafRate(), ci.lower, ci.upper,
                       this->m_parityOk, this->m_parityProbes, this->parityOkRate(),
                       this->assistCov(), this->trustRate(), this->m_nOpp, this->m_nTrustOpp,
                       this->m_maxAbsDx, this->m_maxAbsDy, EbAssistContracts140::statusId(g5),
                       EbAssistContracts140::statusId(g6), EbAssistContracts140::statusId(g7a),
                       EbAssistContracts140::statusId(worst),
                       EbAssistContracts140::isSoloCorpus(corpus) ? "solo" : "multi");
}

std::string EbAssistRealMetrics::reportLiveLine(char const* corpusLabel) const {
    char const* corpus =
        corpusLabel && corpusLabel[0] != '\0' ? corpusLabel : "live";
    return fmt::format(
        "corpus={} live held={} asMade={} passThrough={} rescue={:.4f} "
        "falseMut={}/{}={:.4f} passHonest={:.4f} "
        "windowLegal={}/{}={:.4f} passBudget={} passUnknown={} passUntrust={} "
        "simEstimate=never",
        corpus, this->m_liveHeld, this->m_liveAsMade, this->m_livePassThrough,
        this->liveRescueRate(), this->m_liveFalseMutation, this->m_arms,
        this->liveFalseMutationRate(), this->livePassHonestyRate(), this->m_liveWindowLegal,
        this->m_liveWindowLegal + this->m_liveWindowIllegal, this->liveWindowLegalityRate(),
        this->m_livePassBudget, this->m_livePassUnknown, this->m_livePassUntrust);
}

std::string EbAssistRealMetrics::overlayBit() const {
    if (this->m_arms <= 0 && this->m_parityProbes <= 0 && this->m_nOpp <= 0 &&
        this->m_liveHeld <= 0 && this->m_liveAsMade <= 0) {
        return {};
    }
    return fmt::format("daf={}/{} cov={:.0f}% trust={:.0f}% par={}/{} held={} win={}/{}",
                       this->m_daf, this->m_arms, this->assistCov() * 100.f,
                       this->trustRate() * 100.f, this->m_parityOk, this->m_parityProbes,
                       this->m_liveHeld, this->m_liveWindowLegal,
                       this->m_liveWindowLegal + this->m_liveWindowIllegal);
}

bool EbAssistRealMetrics::selfCheck(std::string& report) {
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

    EbAssistRealMetrics& m = EbAssistRealMetrics::get();
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

    m.noteLiveDecision(EbLiveStageParity::DecisionKind::Held, true);
    m.noteLiveDecision(EbLiveStageParity::DecisionKind::Held, false);
    m.noteLiveDecision(EbLiveStageParity::DecisionKind::AsMade, false);
    m.noteLiveDecision(EbLiveStageParity::DecisionKind::PassThrough, false);
    expect(m.liveHeld() == 2 && m.liveAsMade() == 1 && m.livePassThrough() == 1, "live-decisions");
    expect(m.liveWindowLegal() == 1 && m.liveWindowIllegal() == 1, "live-window");
    expect(std::fabs(m.liveWindowLegalityRate() - 0.5f) < 1e-4f, "live-win-rate");
    m.noteLivePassHonesty(AssistReason::PassBudget);
    m.noteLivePassHonesty(AssistReason::PassUnknown);
    m.noteLivePassHonesty(AssistReason::PassUntrust);
    expect(m.livePassBudget() == 1 && m.livePassUnknown() == 1 && m.livePassUntrust() == 1,
           "live-pass-honesty");
    m.noteLiveFalseMutation();
    expect(m.liveFalseMutation() == 1, "live-false-mut");
    expect(m.liveRescueRate() > 0.f, "live-rescue");

    auto const line = m.reportLine("solo");
    expect(line.find("corpus=solo") != std::string::npos, "line-corpus");
    expect(line.find("sample=solo") != std::string::npos, "line-sample");
    expect(line.find("simEstimate") == std::string::npos, "no-sim-estimate");
    auto const liveLine = m.reportLiveLine("live-nl2");
    expect(liveLine.find("corpus=live-nl2") != std::string::npos, "live-line-corpus");
    expect(liveLine.find("simEstimate=never") != std::string::npos, "live-no-sim-est");

    m.resetSession();
    expect(m.nOpp() == 0 && m.daf() == 0 && m.liveHeld() == 0, "reset");

    if (fails == 0) {
        report = "AssistRealMetrics selfCheck OK";
        return true;
    }
    report = fmt::format("AssistRealMetrics selfCheck {} fails: {}", fails, report);
    return false;
}
