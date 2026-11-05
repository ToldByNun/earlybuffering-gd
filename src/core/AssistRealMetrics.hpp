#ifndef EARLYBUFFER_CORE_ASSISTREALMETRICS_HPP
#define EARLYBUFFER_CORE_ASSISTREALMETRICS_HPP

#include "../config/StageConfig.hpp"
#include "AssistContracts140.hpp"

#include <array>
#include <string>
#include <unordered_map>

// Runtime counters for real (observed) assist contracts — never mix with simEstimate.
class AssistRealMetrics {
public:
    struct ModeAcc {
        int nOpp = 0;
        int nTrustOpp = 0;
        int arms = 0;
        int daf = 0;
    };

    static AssistRealMetrics& get();

    void resetSession();
    void setLevelKey(std::string key, bool dynamicLabelled = false);

    // Opportunity press (Trust optional). Call once per press decision.
    void noteOpportunity(bool trusted, GameModeKind mode = GameModeKind::Cube);
    // Delay/HUS→Delay armed (buffered).
    void noteArm(GameModeKind mode = GameModeKind::Cube);
    // Real death after a fired Delay inject.
    void noteDeathAfterFire(GameModeKind mode = GameModeKind::Cube);
    // GhostParityProbe finished one probe.
    void noteParityProbe(bool drifted, float absDx = 0.f, float absDy = 0.f);

    int nOpp() const { return this->m_nOpp; }
    int nTrustOpp() const { return this->m_nTrustOpp; }
    int arms() const { return this->m_arms; }
    int daf() const { return this->m_daf; }
    int parityProbes() const { return this->m_parityProbes; }
    int parityOk() const { return this->m_parityOk; }
    int driftEvents() const { return this->m_driftEvents; }

    ModeAcc const& modeAcc(GameModeKind mode) const;

    float trustRate() const;
    float assistCov() const;
    float dafRate() const;
    float parityOkRate() const;

    float trustRateFor(GameModeKind mode) const;
    float assistCovFor(GameModeKind mode) const;
    float dafRateFor(GameModeKind mode) const;

    // Pose-noise trackers for ε remeasure (next version only).
    float maxAbsDx() const { return this->m_maxAbsDx; }
    float maxAbsDy() const { return this->m_maxAbsDy; }

    // One-line report: rates + gate statuses + corpus= + per-mode splits.
    // G10 simEstimate never here. Includes AssistContracts142 freeze bit.
    std::string reportLine(char const* corpusLabel = nullptr) const;

    // Overlay fragment, e.g. "daf=0/12 cov~1.00".
    std::string overlayBit() const;

    static bool selfCheck(std::string& report);

private:
    AssistRealMetrics() = default;

    static int modeIndex(GameModeKind mode);
    ModeAcc& modeAccMut(GameModeKind mode);

    struct LevelAcc {
        int nOpp = 0;
        int nTrustOpp = 0;
        bool dynamicLabelled = false;
    };

    int m_nOpp = 0;
    int m_nTrustOpp = 0;
    int m_arms = 0;
    int m_daf = 0;
    int m_parityProbes = 0;
    int m_parityOk = 0;
    int m_driftEvents = 0;
    float m_maxAbsDx = 0.f;
    float m_maxAbsDy = 0.f;
    std::string m_levelKey;
    std::unordered_map<std::string, LevelAcc> m_levels;
    std::array<ModeAcc, StageConfig::MODE_COUNT> m_modes{};
};

#endif // EARLYBUFFER_CORE_ASSISTREALMETRICS_HPP
