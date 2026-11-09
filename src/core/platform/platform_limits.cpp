#include "../../app/class_manager.hpp"
#include "platform_limits.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>

EbPlatformLimits& EbPlatformLimits::get() {
    return EbClassManager::get().service<EbPlatformLimits>();
}

bool EbPlatformLimits::isConstrainedDevice() {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    return true;
#else
    return false;
#endif
}

bool EbPlatformLimits::allowLoadDiskSelfCheck() const {
    (void)this;
    return !EbPlatformLimits::isConstrainedDevice();
}

int EbPlatformLimits::clampSimBudgetUs(int requestedUs) const {
    (void)this;
    if (requestedUs <= 0)
        return requestedUs; // 0 = unlimited wall clock
    if (EbPlatformLimits::isConstrainedDevice()) {
        return std::min(requestedUs, kConstrainedMaxSimBudgetUs);
    }
    return requestedUs;
}

int EbPlatformLimits::clampTickBudget(int requestedTicks) const {
    (void)this;
    if (EbPlatformLimits::isConstrainedDevice()) {
        if (requestedTicks <= 0)
            return kConstrainedMaxTickBudget;
        return std::min(requestedTicks, kConstrainedMaxTickBudget);
    }
    return requestedTicks;
}

float EbPlatformLimits::overlayTopInset() const {
    (void)this;
    if (EbPlatformLimits::isConstrainedDevice()) {
        return kConstrainedOverlayTopInset;
    }
    return kDefaultOverlayTopInset;
}

float EbPlatformLimits::overlayLeftInset() const {
    (void)this;
    return kDefaultOverlayLeftInset;
}

bool EbPlatformLimits::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    auto& lim = EbPlatformLimits::get();
    expect(lim.clampSimBudgetUs(0) == 0, "sim-unlimited");
    expect(lim.clampSimBudgetUs(500) == 500, "sim-below-cap");
    expect(lim.overlayLeftInset() == kDefaultOverlayLeftInset, "overlay-left");

    bool const mobile = EbPlatformLimits::isConstrainedDevice();
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    expect(mobile, "constrained-compile-match");
#else
    expect(!mobile, "desktop-compile-match");
#endif

    if (mobile) {
        expect(!lim.allowLoadDiskSelfCheck(), "mobile-no-disk-gold");
        expect(lim.clampSimBudgetUs(10000) == kConstrainedMaxSimBudgetUs, "mobile-sim-cap");
        expect(lim.clampTickBudget(20000) == kConstrainedMaxTickBudget, "mobile-tick-cap");
        expect(lim.overlayTopInset() == kConstrainedOverlayTopInset, "overlay-top-mobile");
#if defined(GEODE_IS_ANDROID)
        expect(mobile, "label-android-as-mobile");
#elif defined(GEODE_IS_IOS)
        expect(mobile, "label-ios-as-mobile");
#endif
    } else {
        expect(lim.allowLoadDiskSelfCheck(), "desktop-disk-gold-ok");
        expect(lim.clampSimBudgetUs(10000) == 10000, "desktop-sim-passthrough");
        expect(lim.clampTickBudget(20000) == 20000, "desktop-tick-passthrough");
        expect(lim.overlayTopInset() == kDefaultOverlayTopInset, "overlay-top-desktop");
#if defined(GEODE_IS_MACOS)
        expect(!mobile, "macos-not-constrained");
#endif
    }

    if (fails == 0) {
        report = mobile ? "PlatformLimits selfCheck OK (mobile)"
                        : "PlatformLimits selfCheck OK (desktop)";
        return true;
    }
    report = std::string("PlatformLimits selfCheck fails: ") + report;
    return false;
}
