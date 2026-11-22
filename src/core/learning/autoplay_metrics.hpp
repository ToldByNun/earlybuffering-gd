#ifndef AUTOPLAY_METRICS_HPP
#define AUTOPLAY_METRICS_HPP

#include <string>

// Assist Pack AP-NL6 — Autoplay-only DR4 metrics (never mixed into Live reports).
// Route fidelity / verify completeness / progress; seed counters are diagnostics only.
class EbAutoplayMetrics {
public:
    void reset();

    // Verify completeness buckets (one note per arm/probe outcome).
    void noteVerifyComplete(); // ShadowVerified arm
    void noteVerifyPartial();  // PartiallyChecked / prefix-short
    void noteVerifyBudget();   // budget abort
    void noteVerifyOther();    // no-model / rejected / etc.

    // Executed sequence == verified sequence (order + relative delays).
    void noteFidelity(bool match);

    // Progress under verified routes (secondary).
    void noteProgress(float dx, int ticksSurvived);

    // Seed quality diagnostics — never treat as survival.
    void noteSeedDiag(int novaParityAccepts, int enhancedProposes, int postVerifyArms);

    void noteStuckRecovery();
    void noteQueueArm();
    void noteMemoryReuse();
    void noteCpsThrottle();

    int verifyComplete() const {
        return m_verifyComplete;
    }
    int verifyPartial() const {
        return m_verifyPartial;
    }
    int verifyBudget() const {
        return m_verifyBudget;
    }
    int verifyOther() const {
        return m_verifyOther;
    }
    int fidelityOk() const {
        return m_fidelityOk;
    }
    int fidelityFail() const {
        return m_fidelityFail;
    }
    int stuckRecoveries() const {
        return m_stuckRecoveries;
    }
    int queueArms() const {
        return m_queueArms;
    }
    int memoryReuses() const {
        return m_memoryReuses;
    }
    int cpsThrottles() const {
        return m_cpsThrottles;
    }
    float progressPx() const {
        return m_progressPx;
    }
    int progressTicks() const {
        return m_progressTicks;
    }
    int seedNovaAccepts() const {
        return m_seedNovaAccepts;
    }
    int seedEnhancedProposes() const {
        return m_seedEnhancedProposes;
    }
    int seedPostVerifyArms() const {
        return m_seedPostVerifyArms;
    }

    // DR4 rates (denominators floored at 1).
    float routeFidelityRate() const;
    float verifyCompletenessRate() const;

    // Autoplay-only line — never includes Live rescue/window fields.
    std::string reportAutoplayLine(char const* corpusLabel = nullptr) const;

    static bool selfCheck(std::string& report);

private:
    int m_verifyComplete = 0;
    int m_verifyPartial = 0;
    int m_verifyBudget = 0;
    int m_verifyOther = 0;
    int m_fidelityOk = 0;
    int m_fidelityFail = 0;
    int m_stuckRecoveries = 0;
    int m_queueArms = 0;
    int m_memoryReuses = 0;
    int m_cpsThrottles = 0;
    float m_progressPx = 0.f;
    int m_progressTicks = 0;
    int m_seedNovaAccepts = 0;
    int m_seedEnhancedProposes = 0;
    int m_seedPostVerifyArms = 0;

    int verifyAttempts() const {
        return m_verifyComplete + m_verifyPartial + m_verifyBudget + m_verifyOther;
    }
};

#endif
