#ifndef EARLYBUFFER_CORE_ASSISTCONTRACTS140_HPP
#define EARLYBUFFER_CORE_ASSISTCONTRACTS140_HPP

#include <string>

// Frozen v1.4.0 assist contracts (Commit 2 LOCK).
// DO NOT RETUNE thresholds without a new mod version + new contracts doc.
class AssistContracts140 {
public:
    static constexpr int kSchema = 1;
    static constexpr char const* kCorpusLabel = "solo"; // solo until multi opt-in lands

    // --- Locked ε from Commit-1 baseline placeholder (p99 pending multi-corpus) ---
    // Changing these requires a new version; GhostParityProbe reads these at init.
    static constexpr float kLockedEpsX = 12.f;
    static constexpr float kLockedEpsY = 12.f;

    // --- Sample sizes ---
    static constexpr int kAMinMulti = 100; // fired Delays across corpus
    static constexpr int kAMinSolo = 40;   // with corpus=solo label
    static constexpr int kOppMinPerLevel = 20;

    // --- G5–G9 freeze candidates (LOCKED) ---
    static constexpr float kG5DafCiUpperMax = 0.02f;    // Wilson CI upper ≤ 2%
    static constexpr float kG6ParityOkMin = 0.99f;      // static corpus
    static constexpr float kG7aAssistCovMin = 0.95f;    // on N_trust_opp
    static constexpr float kG7bTrustRateStaticMin = 0.70f;
    static constexpr float kG7bTrustRateDynamicMin = 0.10f;
    static constexpr float kG8BudgetHitRelSlack = 0.10f; // ≤ baseline + 10% rel

    enum class GateStatus {
        Pass = 0,
        Fail,
        Inconclusive
    };

    struct WilsonInterval {
        float lower = 0.f;
        float upper = 1.f;
        float phat = 0.f;
        int n = 0;
        int x = 0;
    };

    // Wilson score 95% CI for binomial x/n (z≈1.96).
    static WilsonInterval wilson95(int x, int n);

    static int aMinForCorpus(char const* corpusLabel);
    static bool isSoloCorpus(char const* corpusLabel);

    // G5: DAFRate CI upper ≤ kG5DafCiUpperMax when A ≥ A_min; else INCONCLUSIVE.
    static GateStatus gateG5Daf(int daf, int arms, char const* corpusLabel);

    // G6: ParityOKRate ≥ kG6ParityOkMin when probes ≥ 1; else INCONCLUSIVE.
    static GateStatus gateG6Parity(int okProbes, int totalProbes);

    // G7a: AssistCov ≥ kG7aAssistCovMin when N_trust_opp ≥ 1; else INCONCLUSIVE.
    static GateStatus gateG7aAssistCov(int arms, int trustOpp);

    // G7b: TrustRate ≥ min for level class when N_opp ≥ Opp_min; else INCONCLUSIVE.
    static GateStatus gateG7bTrustRate(
        int trustOpp, int opp, bool dynamicLabelled
    );

    static char const* statusId(GateStatus s);

    // Apply locked ε to GhostParityProbe (call once at boot / PlanGold).
    static void applyLockedEps();

    static bool selfCheck(std::string& report);

private:
    AssistContracts140() = delete;
};

#endif // EARLYBUFFER_CORE_ASSISTCONTRACTS140_HPP
