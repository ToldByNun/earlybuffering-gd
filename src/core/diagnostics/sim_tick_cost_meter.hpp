#ifndef SIM_TICK_COST_METER_HPP
#define SIM_TICK_COST_METER_HPP

#include <string>

// Rolling µs / shadow physics tick for Adaptive BudgetController.
// AP1 is measure-only: overlay + getters. Does not change plan limits or search.
class EbSimTickCostMeter {
public:
    static EbSimTickCostMeter& get();

    // One EbShadowPhysics::stepPhysics wall sample (microseconds, clamped ≥0).
    void notePhysicsTick(int micros);

    // End of a Live press plan. Classic proxy: fullyVerified = tried when !budgetHit,
    // else 0 (remaining delays were cut short). Adaptive will pass real coverage later.
    void notePlanOutcome(bool budgetHit, int candidatesTried, int candidatesFullyVerified);

    void notePhysicsPhases(int worldUs, int updateUs, int collisionUs);
    long long worldTotalUs() const;
    long long updateTotalUs() const;
    long long collisionTotalUs() const;
    long long physicsTotalCount() const;

    float emaUsPerTick() const;
    float stddevUsPerTick() const;
    float budgetHitRate() const;
    float fullyVerifiedRate() const;
    int physicsSampleCount() const;
    int planSampleCount() const;
    std::string overlayBit() const;

    static bool selfCheck(std::string& report);

private:
    friend class EbServiceStore;

    static constexpr int kTickRing = 64;
    static constexpr int kPlanRing = 32;
    static constexpr float kEmaAlpha = 0.15f;

    long long m_worldTotalUs = 0;
    long long m_updateTotalUs = 0;
    long long m_collisionTotalUs = 0;
    long long m_physicsTotalCount = 0;
    float m_emaUs = 0.f;
    bool m_emaInit = false;

    int m_tickUs[kTickRing]{};
    int m_tickI = 0;
    int m_tickCount = 0;

    bool m_hit[kPlanRing]{};
    int m_tried[kPlanRing]{};
    int m_full[kPlanRing]{};
    int m_planI = 0;
    int m_planCount = 0;
};

#endif
