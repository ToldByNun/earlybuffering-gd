#ifndef EARLYBUFFER_CORE_PLATFORMLIMITS_HPP
#define EARLYBUFFER_CORE_PLATFORMLIMITS_HPP

#include <string>

// Platform ceilings / load-path policy.
// Constrained devices = Android + iOS (not Windows, not macOS): tighter planner
// budgets, taller overlay inset, no disk PlanGold on mod load.
class PlatformLimits {
public:
    static PlatformLimits& get();

    // Android and iOS. macOS / Windows → false (passthrough budgets).
    static bool isConstrainedDevice();

    // When false, DevTraceStore / DevCycleRecorder selfChecks must stay in-memory
    // (no beginSession/flush/reload). Used on constrained $on_mod(Loaded).
    bool allowLoadDiskSelfCheck() const;

    // Ceiling for StageConfig::simBudgetUs() (0 = unlimited stays 0).
    int clampSimBudgetUs(int requestedUs) const;

    // Ceiling for StageConfig::tickBudgetPerPress().
    int clampTickBudget(int requestedTicks) const;

    // Status overlay distance from top / left of the win size (points).
    float overlayTopInset() const;
    float overlayLeftInset() const;

    static bool selfCheck(std::string& report);

private:
    PlatformLimits() = default;

    // Same numeric ceilings as the prior Android-only policy.
    static constexpr int kConstrainedMaxSimBudgetUs = 2000;
    static constexpr int kConstrainedMaxTickBudget = 2000;
    static constexpr float kDefaultOverlayTopInset = 10.f;
    static constexpr float kConstrainedOverlayTopInset = 28.f;
    static constexpr float kDefaultOverlayLeftInset = 10.f;
};

#endif // EARLYBUFFER_CORE_PLATFORMLIMITS_HPP
