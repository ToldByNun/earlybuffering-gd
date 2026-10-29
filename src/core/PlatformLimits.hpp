#ifndef EARLYBUFFER_CORE_PLATFORMLIMITS_HPP
#define EARLYBUFFER_CORE_PLATFORMLIMITS_HPP

#include <string>

// Platform ceilings / load-path policy. Windows/mac/iOS: passthrough.
// Android: tighter planner budgets + no disk PlanGold on mod load.
class PlatformLimits {
public:
    static PlatformLimits& get();

    // When false, DevTraceStore / DevCycleRecorder selfChecks must stay in-memory
    // (no beginSession/flush/reload). Used on Android $on_mod(Loaded).
    bool allowLoadDiskSelfCheck() const;

    // Ceiling for StageConfig::simBudgetUs() (0 = unlimited stays 0).
    int clampSimBudgetUs(int requestedUs) const;

    // Ceiling for StageConfig::tickBudgetPerPress().
    int clampTickBudget(int requestedTicks) const;

    static bool selfCheck(std::string& report);

private:
    PlatformLimits() = default;

    static constexpr int kAndroidMaxSimBudgetUs = 2000;
    static constexpr int kAndroidMaxTickBudget = 2000;
};

#endif // EARLYBUFFER_CORE_PLATFORMLIMITS_HPP
