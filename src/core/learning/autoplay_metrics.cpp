#include "autoplay_metrics.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

void EbAutoplayMetrics::reset() {
    m_verifyComplete = 0;
    m_verifyPartial = 0;
    m_verifyBudget = 0;
    m_verifyOther = 0;
    m_fidelityOk = 0;
    m_fidelityFail = 0;
    m_stuckRecoveries = 0;
    m_queueArms = 0;
    m_memoryReuses = 0;
    m_cpsThrottles = 0;
    m_progressPx = 0.f;
    m_progressTicks = 0;
    m_seedNovaAccepts = 0;
    m_seedEnhancedProposes = 0;
    m_seedPostVerifyArms = 0;
}

void EbAutoplayMetrics::noteVerifyComplete() {
    ++m_verifyComplete;
}
void EbAutoplayMetrics::noteVerifyPartial() {
    ++m_verifyPartial;
}
void EbAutoplayMetrics::noteVerifyBudget() {
    ++m_verifyBudget;
}
void EbAutoplayMetrics::noteVerifyOther() {
    ++m_verifyOther;
}
void EbAutoplayMetrics::noteFidelity(bool match) {
    if (match)
        ++m_fidelityOk;
    else
        ++m_fidelityFail;
}
void EbAutoplayMetrics::noteProgress(float dx, int ticksSurvived) {
    if (dx > 0.f)
        m_progressPx += dx;
    if (ticksSurvived > 0)
        m_progressTicks += ticksSurvived;
}
void EbAutoplayMetrics::noteSeedDiag(int novaParityAccepts, int enhancedProposes,
                                     int postVerifyArms) {
    m_seedNovaAccepts += std::max(0, novaParityAccepts);
    m_seedEnhancedProposes += std::max(0, enhancedProposes);
    m_seedPostVerifyArms += std::max(0, postVerifyArms);
}
void EbAutoplayMetrics::noteStuckRecovery() {
    ++m_stuckRecoveries;
}
void EbAutoplayMetrics::noteQueueArm() {
    ++m_queueArms;
}
void EbAutoplayMetrics::noteMemoryReuse() {
    ++m_memoryReuses;
}
void EbAutoplayMetrics::noteCpsThrottle() {
    ++m_cpsThrottles;
}

float EbAutoplayMetrics::routeFidelityRate() const {
    int const n = m_fidelityOk + m_fidelityFail;
    return static_cast<float>(m_fidelityOk) / static_cast<float>(std::max(1, n));
}

float EbAutoplayMetrics::verifyCompletenessRate() const {
    return static_cast<float>(m_verifyComplete) /
           static_cast<float>(std::max(1, verifyAttempts()));
}

std::string EbAutoplayMetrics::reportAutoplayLine(char const* corpusLabel) const {
    char const* corpus =
        corpusLabel && corpusLabel[0] != '\0' ? corpusLabel : "autoplay";
    return fmt::format(
        "corpus={} autoplay fidelity={}/{}={:.4f} verifyComplete={}/{}={:.4f} "
        "partial={} budget={} other={} progressPx={:.1f} progressTicks={} "
        "stuck={} queue={} memory={} cpsThrottle={} "
        "seedDiag nova={} enh={} postVerify={} live=never",
        corpus, m_fidelityOk, m_fidelityOk + m_fidelityFail, routeFidelityRate(),
        m_verifyComplete, verifyAttempts(), verifyCompletenessRate(), m_verifyPartial,
        m_verifyBudget, m_verifyOther, m_progressPx, m_progressTicks, m_stuckRecoveries,
        m_queueArms, m_memoryReuses, m_cpsThrottles, m_seedNovaAccepts,
        m_seedEnhancedProposes, m_seedPostVerifyArms);
}

bool EbAutoplayMetrics::selfCheck(std::string& report) {
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

    EbAutoplayMetrics m;
    expect(m.verifyComplete() == 0 && m.routeFidelityRate() == 1.f, "fresh");
    m.noteVerifyComplete();
    m.noteVerifyPartial();
    m.noteVerifyBudget();
    m.noteVerifyOther();
    expect(m.verifyComplete() == 1 && m.verifyPartial() == 1, "verify-buckets");
    expect(std::fabs(m.verifyCompletenessRate() - 0.25f) < 1e-4f, "verify-rate");
    m.noteFidelity(true);
    m.noteFidelity(true);
    m.noteFidelity(false);
    expect(m.fidelityOk() == 2 && m.fidelityFail() == 1, "fidelity-count");
    expect(std::fabs(m.routeFidelityRate() - (2.f / 3.f)) < 1e-4f, "fidelity-rate");
    m.noteProgress(12.5f, 40);
    expect(m.progressPx() >= 12.f && m.progressTicks() == 40, "progress");
    m.noteSeedDiag(3, 5, 2);
    expect(m.seedNovaAccepts() == 3 && m.seedEnhancedProposes() == 5 &&
               m.seedPostVerifyArms() == 2,
           "seed-diag");
    m.noteStuckRecovery();
    m.noteQueueArm();
    m.noteMemoryReuse();
    m.noteCpsThrottle();
    expect(m.stuckRecoveries() == 1 && m.queueArms() == 1 && m.memoryReuses() == 1 &&
               m.cpsThrottles() == 1,
           "ops");

    auto const line = m.reportAutoplayLine("ap-nl6");
    expect(line.find("corpus=ap-nl6") != std::string::npos, "line-corpus");
    expect(line.find("live=never") != std::string::npos, "no-live-mix");
    expect(line.find("rescue=") == std::string::npos, "no-live-rescue");
    expect(line.find("fidelity=") != std::string::npos, "has-fidelity");

    m.reset();
    expect(m.verifyComplete() == 0 && m.fidelityOk() == 0 && m.seedNovaAccepts() == 0, "reset");

    if (fails == 0) {
        report = "AutoplayMetrics selfCheck OK";
        return true;
    }
    report = fmt::format("AutoplayMetrics selfCheck {} fails: {}", fails, report);
    return false;
}
