#include "PlatformLimits.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>

PlatformLimits& PlatformLimits::get() {
    static PlatformLimits inst;
    return inst;
}

bool PlatformLimits::allowLoadDiskSelfCheck() const {
#ifdef GEODE_IS_ANDROID
    (void)this;
    return false;
#else
    (void)this;
    return true;
#endif
}

int PlatformLimits::clampSimBudgetUs(int requestedUs) const {
    (void)this;
    if (requestedUs <= 0) return requestedUs; // 0 = unlimited wall clock
#ifdef GEODE_IS_ANDROID
    return std::min(requestedUs, kAndroidMaxSimBudgetUs);
#else
    return requestedUs;
#endif
}

int PlatformLimits::clampTickBudget(int requestedTicks) const {
    (void)this;
#ifdef GEODE_IS_ANDROID
    if (requestedTicks <= 0) return kAndroidMaxTickBudget;
    return std::min(requestedTicks, kAndroidMaxTickBudget);
#else
    return requestedTicks;
#endif
}

bool PlatformLimits::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    auto& lim = PlatformLimits::get();
    expect(lim.clampSimBudgetUs(0) == 0, "sim-unlimited");
    expect(lim.clampSimBudgetUs(500) == 500, "sim-below-cap");
#ifdef GEODE_IS_ANDROID
    expect(!lim.allowLoadDiskSelfCheck(), "android-no-disk-gold");
    expect(lim.clampSimBudgetUs(10000) == kAndroidMaxSimBudgetUs, "sim-cap");
    expect(lim.clampTickBudget(20000) == kAndroidMaxTickBudget, "tick-cap");
#else
    expect(lim.allowLoadDiskSelfCheck(), "disk-gold-ok");
    expect(lim.clampSimBudgetUs(10000) == 10000, "sim-passthrough");
    expect(lim.clampTickBudget(20000) == 20000, "tick-passthrough");
#endif

    if (fails == 0) {
        report = "PlatformLimits selfCheck OK";
        return true;
    }
    report = std::string("PlatformLimits selfCheck fails: ") + report;
    return false;
}
