#include "adaptive_budget_controller.hpp"
#include "../../../../diagnostics/sim_tick_cost_meter.hpp"
#include "../../../../platform/platform_limits.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

int EbAdaptiveBudgetController::Envelope::totalLeft() const {
    return screenLeft + verifyLeft + reserveLeft;
}

int EbAdaptiveBudgetController::Envelope::phaseLeft(Phase phase) const {
    switch (phase) {
    case Phase::Screen:
        return screenLeft;
    case Phase::Verify:
        return verifyLeft;
    case Phase::Reserve:
        return reserveLeft;
    }
    return 0;
}

bool EbAdaptiveBudgetController::Envelope::spend(Phase phase, int ticks) {
    if (ticks <= 0)
        return true;
    int* bucket = nullptr;
    switch (phase) {
    case Phase::Screen:
        bucket = &screenLeft;
        break;
    case Phase::Verify:
        bucket = &verifyLeft;
        break;
    case Phase::Reserve:
        bucket = &reserveLeft;
        break;
    }
    if (!bucket || *bucket <= 0)
        return false;
    int const take = std::min(ticks, *bucket);
    *bucket -= take;
    return take > 0;
}

int EbAdaptiveBudgetController::Envelope::takeQuantum(Phase phase) const {
    int const left = phaseLeft(phase);
    if (left <= 0)
        return 0;
    if (phase == Phase::Screen)
        return std::min(left, std::max(1, screenQuantum));
    if (phase == Phase::Verify)
        return std::min(left, std::max(1, verifyQuantum));
    return left;
}

EbAdaptiveBudgetController::Envelope EbAdaptiveBudgetController::plan(
    int tickCeiling, int usCeiling, float emaUsPerTick, int screenQuantum, int verifyQuantum,
    int reservePct) {
    Envelope env;
    env.screenQuantum = std::clamp(screenQuantum, 8, 64);
    env.verifyQuantum = std::clamp(verifyQuantum, 8, 64);
    env.emaUsPerTick = std::max(0.f, emaUsPerTick);
    env.budgetUs = usCeiling;

    // Clamp the ceiling once. Never call clampTickBudget(0) — on mobile that
    // would raise a zero allowance to the platform max.
    int const rawCeil = std::max(0, tickCeiling);
    int const tickCeil =
        rawCeil > 0 ? EbPlatformLimits::get().clampTickBudget(rawCeil) : 0;
    // Hard ticks = Stage/Platform tick ceiling. Do NOT shrink by EMA×wall —
    // expensive levels (~50–200µs/tick) made allowedTicks tiny and Adaptive
    // returned PassBudget on every press. Soft wall stays budgetUs at runtime.
    int allowed = tickCeil;
    if (usCeiling > 0 && env.emaUsPerTick >= kMinEmaUs && tickCeil > 0) {
        env.suggestedTicksFromWall =
            static_cast<int>(std::floor(static_cast<double>(usCeiling) / env.emaUsPerTick));
    }
    env.allowedTicks = allowed;

    int const reservePctClamped = std::clamp(reservePct, 0, 40);
    int reserve = (allowed * reservePctClamped) / 100;
    if (reserve > allowed)
        reserve = allowed;
    int const work = allowed - reserve;
    int screen = (work * kScreenWorkSharePct) / 100;
    if (screen > work)
        screen = work;
    int verify = work - screen;

    env.screenTicks = std::max(kMinBucketTicks, screen);
    env.verifyTicks = std::max(kMinBucketTicks, verify);
    env.reserveTicks = std::max(kMinBucketTicks, reserve);
    env.screenLeft = env.screenTicks;
    env.verifyLeft = env.verifyTicks;
    env.reserveLeft = env.reserveTicks;
    return env;
}

EbAdaptiveBudgetController::Envelope
EbAdaptiveBudgetController::planFromOptions(EbPlanOptions const& options) {
    int tickCeiling = options.tickBudget;
    if (tickCeiling <= 0)
        tickCeiling = 4000;
    tickCeiling = EbPlatformLimits::get().clampTickBudget(tickCeiling);

    int usCeiling = options.budgetUs;
    if (usCeiling > 0)
        usCeiling = EbPlatformLimits::get().clampSimBudgetUs(usCeiling);

    float ema = 0.f;
    if (EbSimTickCostMeter::get().physicsSampleCount() > 0)
        ema = EbSimTickCostMeter::get().emaUsPerTick();

    return plan(tickCeiling, usCeiling, ema, options.adaptiveScreenTicks,
                options.adaptiveVerifyQuantum, options.adaptiveReservePct);
}

bool EbAdaptiveBudgetController::selfCheck(std::string& report) {
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

    // Full tick ceiling when no EMA / unlimited wall.
    auto const full = plan(4000, 0, 0.f, 24, 16, 15);
    expect(full.allowedTicks == EbPlatformLimits::get().clampTickBudget(4000), "full-ceil");
    expect(full.allowedTicks == full.screenTicks + full.verifyTicks + full.reserveTicks,
           "full-split-sum");
    expect(full.reserveTicks == (full.allowedTicks * 15) / 100, "reserve-15");
    expect(full.screenLeft == full.screenTicks && full.verifyLeft == full.verifyTicks, "left-init");

    // Soft wall must NOT shrink hard tick allowance (Assist starvation bug).
    auto const wall = plan(4000, 1500, 1.f, 24, 16, 15); // EMA suggests 1500, hard stays 4000
    expect(wall.allowedTicks == EbPlatformLimits::get().clampTickBudget(4000), "wall-soft");
    expect(wall.suggestedTicksFromWall == 1500, "wall-suggest");
    expect(wall.budgetUs == 1500, "wall-us");
    expect(wall.allowedTicks == wall.screenTicks + wall.verifyTicks + wall.reserveTicks,
           "wall-split-sum");

    // Unlimited soft wall (sim-budget-us=0) still keeps full hard ticks.
    auto const unlimited = plan(2500, 0, 200.f, 24, 16, 15);
    expect(unlimited.budgetUs == 0, "unlimited-us");
    expect(unlimited.allowedTicks == EbPlatformLimits::get().clampTickBudget(2500),
           "unlimited-ticks");
    expect(unlimited.suggestedTicksFromWall == 0, "unlimited-no-suggest");

    // Expensive EMA must not collapse ticks under a tight wall.
    auto const expensive = plan(2500, 3000, 150.f, 24, 16, 15);
    expect(expensive.allowedTicks == EbPlatformLimits::get().clampTickBudget(2500),
           "expensive-no-shrink");
    expect(expensive.suggestedTicksFromWall == 20, "expensive-suggest");

    // Tiny EMA ignored for suggestion; hard ticks unchanged.
    auto const tiny = plan(500, 10000, 0.01f, 24, 16, 10);
    expect(tiny.allowedTicks == EbPlatformLimits::get().clampTickBudget(500), "tiny-ema-ignore");
    expect(tiny.suggestedTicksFromWall == 0, "tiny-no-suggest");

    // Reserve 0 → all work in screen+verify.
    auto const noRes = plan(1000, 0, 0.f, 24, 16, 0);
    expect(noRes.reserveTicks == 0, "reserve-0");
    expect(noRes.screenTicks + noRes.verifyTicks == noRes.allowedTicks, "work-all");

    // Spend helpers.
    Envelope spendEnv = plan(100, 0, 0.f, 24, 16, 20);
    int const screenBefore = spendEnv.screenLeft;
    expect(spendEnv.spend(Phase::Screen, 10), "spend-screen");
    expect(spendEnv.screenLeft == screenBefore - 10, "spend-screen-left");
    expect(spendEnv.takeQuantum(Phase::Verify) <= spendEnv.verifyQuantum, "verify-quantum");
    while (spendEnv.screenLeft > 0)
        expect(spendEnv.spend(Phase::Screen, 1), "drain-screen");
    expect(!spendEnv.spend(Phase::Screen, 1), "screen-empty");

    // Options path respects opts ceilings and never grows them.
    EbPlanOptions opts;
    opts.tickBudget = 800;
    opts.budgetUs = 0;
    opts.adaptiveScreenTicks = 24;
    opts.adaptiveVerifyQuantum = 16;
    opts.adaptiveReservePct = 15;
    auto const fromOpts = planFromOptions(opts);
    expect(fromOpts.allowedTicks <= 800, "opts-le-tick");
    expect(fromOpts.allowedTicks <= EbPlatformLimits::get().clampTickBudget(800), "opts-platform");

    // Mobile clamp: if constrained, 4000 must become ≤2000.
    auto const mobileish = plan(4000, 0, 0.f, 24, 16, 15);
    if (EbPlatformLimits::isConstrainedDevice()) {
        expect(mobileish.allowedTicks <= 2000, "mobile-tick-cap");
    } else {
        expect(mobileish.allowedTicks == 4000, "desktop-tick-passthrough");
    }

    // Three-limit invariant: plan() has no Max Assist / horizon parameters to raise.
    expect(full.screenQuantum == 24 && full.verifyQuantum == 16, "quanta");

    if (fails == 0)
        report = "AdaptiveBudgetController OK";
    else
        report = fmt::format("AdaptiveBudgetController {} fails: {}", fails, report);
    return fails == 0;
}
