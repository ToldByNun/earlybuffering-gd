#ifndef ASSIST_REAL_METRICS_HPP
#define ASSIST_REAL_METRICS_HPP

#include "../assist/contracts/assist_contracts140.hpp"
#include "../assist/contracts/assist_verdict.hpp"
#include "../assist/contracts/live_stage_parity.hpp"
#include <string>
#include <unordered_map>

// Runtime counters for real (observed) assist contracts — never mix with simEstimate.
// AP-NL2 adds Live-only DR4 counters (rescue / false-mutation / Pass honesty /
// window legality) — separate from Autoplay metrics.
class EbAssistRealMetrics {
public:
    static EbAssistRealMetrics& get();

    void resetSession();
    void setLevelKey(std::string key, bool dynamicLabelled = false);

    // Opportunity press (Trust optional). Call once per press decision.
    void noteOpportunity(bool trusted);
    // Delay/HUS→Delay armed (buffered).
    void noteArm();
    // Real death after a fired Delay inject.
    void noteDeathAfterFire();
    // GhostParityProbe finished one probe.
    void noteParityProbe(bool drifted, float absDx = 0.f, float absDy = 0.f);

    // AP-NL2 Live decision + DR4. Call on every Live press/release plan outcome.
    void noteLiveDecision(EbLiveStageParity::DecisionKind kind, bool windowOk);
    // Named Pass honesty buckets (PassBudget / unknown / untrust-class).
    void noteLivePassHonesty(AssistReason reason);
    // Unexpected timing-only inject side effect (pose/hold/trail/pad). DAF is
    // tracked separately; use this for non-death mutations when detected.
    void noteLiveFalseMutation();

    int nOpp() const {
        return this->m_nOpp;
    }
    int nTrustOpp() const {
        return this->m_nTrustOpp;
    }
    int arms() const {
        return this->m_arms;
    }
    int daf() const {
        return this->m_daf;
    }
    int parityProbes() const {
        return this->m_parityProbes;
    }
    int parityOk() const {
        return this->m_parityOk;
    }
    int driftEvents() const {
        return this->m_driftEvents;
    }

    float trustRate() const;
    float assistCov() const;
    float dafRate() const;
    float parityOkRate() const;

    int liveHeld() const {
        return this->m_liveHeld;
    }
    int liveAsMade() const {
        return this->m_liveAsMade;
    }
    int livePassThrough() const {
        return this->m_livePassThrough;
    }
    int liveWindowLegal() const {
        return this->m_liveWindowLegal;
    }
    int liveWindowIllegal() const {
        return this->m_liveWindowIllegal;
    }
    int livePassBudget() const {
        return this->m_livePassBudget;
    }
    int livePassUnknown() const {
        return this->m_livePassUnknown;
    }
    int livePassUntrust() const {
        return this->m_livePassUntrust;
    }
    int liveFalseMutation() const {
        return this->m_liveFalseMutation;
    }

    // DR4 Live rates (denominators floored at 1).
    float liveRescueRate() const;
    float liveFalseMutationRate() const;
    float livePassHonestyRate() const;
    float liveWindowLegalityRate() const;

    // Pose-noise trackers for ε remeasure (next version only).
    float maxAbsDx() const {
        return this->m_maxAbsDx;
    }
    float maxAbsDy() const {
        return this->m_maxAbsDy;
    }

    // One-line report: rates + gate statuses + corpus= label. G10 simEstimate never here.
    std::string reportLine(char const* corpusLabel = nullptr) const;
    // AP-NL2 Live-only DR4 line (never mixed into Autoplay reports).
    std::string reportLiveLine(char const* corpusLabel = nullptr) const;

    // Overlay fragment, e.g. "daf=0/12 cov~1.00".
    std::string overlayBit() const;

    static bool selfCheck(std::string& report);

private:
    friend class EbServiceStore;

    EbAssistRealMetrics() = default;

    class LevelAcc {
    public:
        int nOpp = 0;
        int nTrustOpp = 0;
        bool dynamicLabelled = false;

    private:
    };

    int m_nOpp = 0;
    int m_nTrustOpp = 0;
    int m_arms = 0;
    int m_daf = 0;
    int m_parityProbes = 0;
    int m_parityOk = 0;
    int m_driftEvents = 0;
    int m_liveHeld = 0;
    int m_liveAsMade = 0;
    int m_livePassThrough = 0;
    int m_liveWindowLegal = 0;
    int m_liveWindowIllegal = 0;
    int m_livePassBudget = 0;
    int m_livePassUnknown = 0;
    int m_livePassUntrust = 0;
    int m_liveFalseMutation = 0;
    float m_maxAbsDx = 0.f;
    float m_maxAbsDy = 0.f;
    std::string m_levelKey;
    std::unordered_map<std::string, LevelAcc> m_levels;
};

#endif
