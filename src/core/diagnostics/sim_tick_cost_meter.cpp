#include "sim_tick_cost_meter.hpp"
#include "../../app/class_manager.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

EbSimTickCostMeter& EbSimTickCostMeter::get() {
    return EbClassManager::get().service<EbSimTickCostMeter>();
}

void EbSimTickCostMeter::notePhysicsPhases(int worldUs, int updateUs, int collisionUs) {
    m_worldTotalUs += worldUs;
    m_updateTotalUs += updateUs;
    m_collisionTotalUs += collisionUs;
    ++m_physicsTotalCount;
}
long long EbSimTickCostMeter::worldTotalUs() const {
    return m_worldTotalUs;
}
long long EbSimTickCostMeter::updateTotalUs() const {
    return m_updateTotalUs;
}
long long EbSimTickCostMeter::collisionTotalUs() const {
    return m_collisionTotalUs;
}
long long EbSimTickCostMeter::physicsTotalCount() const {
    return m_physicsTotalCount;
}

void EbSimTickCostMeter::notePhysicsTick(int micros) {
    int const sample = std::max(0, micros);
    m_tickUs[m_tickI] = sample;
    m_tickI = (m_tickI + 1) % kTickRing;
    if (m_tickCount < kTickRing)
        ++m_tickCount;

    float const value = static_cast<float>(sample);
    if (!m_emaInit) {
        m_emaUs = value;
        m_emaInit = true;
        return;
    }
    m_emaUs = m_emaUs + kEmaAlpha * (value - m_emaUs);
}

void EbSimTickCostMeter::notePlanOutcome(bool budgetHit, int candidatesTried,
                                         int candidatesFullyVerified) {
    int const tried = std::max(0, candidatesTried);
    int const full = std::clamp(candidatesFullyVerified, 0, tried);
    m_hit[m_planI] = budgetHit;
    m_tried[m_planI] = tried;
    m_full[m_planI] = full;
    m_planI = (m_planI + 1) % kPlanRing;
    if (m_planCount < kPlanRing)
        ++m_planCount;
}

float EbSimTickCostMeter::emaUsPerTick() const {
    if (!m_emaInit)
        return 0.f;
    return m_emaUs;
}

float EbSimTickCostMeter::stddevUsPerTick() const {
    if (m_tickCount <= 1)
        return 0.f;
    double sum = 0.0;
    for (int index = 0; index < m_tickCount; ++index)
        sum += static_cast<double>(m_tickUs[index]);
    double const mean = sum / static_cast<double>(m_tickCount);
    double accum = 0.0;
    for (int index = 0; index < m_tickCount; ++index) {
        double const delta = static_cast<double>(m_tickUs[index]) - mean;
        accum += delta * delta;
    }
    return static_cast<float>(std::sqrt(accum / static_cast<double>(m_tickCount - 1)));
}

float EbSimTickCostMeter::budgetHitRate() const {
    if (m_planCount <= 0)
        return 0.f;
    int hits = 0;
    for (int index = 0; index < m_planCount; ++index) {
        if (m_hit[index])
            ++hits;
    }
    return static_cast<float>(hits) / static_cast<float>(m_planCount);
}

float EbSimTickCostMeter::fullyVerifiedRate() const {
    if (m_planCount <= 0)
        return 0.f;
    long long triedSum = 0;
    long long fullSum = 0;
    for (int index = 0; index < m_planCount; ++index) {
        triedSum += m_tried[index];
        fullSum += m_full[index];
    }
    if (triedSum <= 0)
        return 0.f;
    return static_cast<float>(fullSum) / static_cast<float>(triedSum);
}

int EbSimTickCostMeter::physicsSampleCount() const {
    return m_tickCount;
}

int EbSimTickCostMeter::planSampleCount() const {
    return m_planCount;
}

std::string EbSimTickCostMeter::overlayBit() const {
    if (!m_emaInit && m_planCount <= 0)
        return {};
    if (!m_emaInit) {
        return fmt::format("simTick hit{:.0f}% full{:.0f}%", budgetHitRate() * 100.f,
                           fullyVerifiedRate() * 100.f);
    }
    if (m_planCount <= 0) {
        return fmt::format("simTick {:.1f}us σ{:.1f}", emaUsPerTick(), stddevUsPerTick());
    }
    return fmt::format("simTick {:.1f}us σ{:.1f} hit{:.0f}% full{:.0f}%", emaUsPerTick(),
                       stddevUsPerTick(), budgetHitRate() * 100.f, fullyVerifiedRate() * 100.f);
}

bool EbSimTickCostMeter::selfCheck(std::string& report) {
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

    EbSimTickCostMeter scratch{};
    expect(scratch.physicsSampleCount() == 0, "empty-ticks");
    expect(scratch.planSampleCount() == 0, "empty-plans");
    expect(scratch.emaUsPerTick() == 0.f, "empty-ema");
    expect(scratch.overlayBit().empty(), "empty-overlay");

    scratch.notePhysicsTick(10);
    scratch.notePhysicsTick(20);
    scratch.notePhysicsTick(-5);
    expect(scratch.physicsSampleCount() == 3, "tick-count");
    expect(scratch.emaUsPerTick() > 0.f, "ema-pos");
    expect(scratch.stddevUsPerTick() >= 0.f, "stddev-nonneg");

    scratch.notePlanOutcome(false, 5, 5);
    scratch.notePlanOutcome(true, 4, 0);
    expect(scratch.planSampleCount() == 2, "plan-count");
    expect(std::fabs(scratch.budgetHitRate() - 0.5f) < 1e-4f, "hit-rate");
    expect(std::fabs(scratch.fullyVerifiedRate() - (5.f / 9.f)) < 1e-4f, "full-rate");

    auto const bit = scratch.overlayBit();
    expect(bit.find("simTick") != std::string::npos, "overlay-tag");

    // Clamp fullyVerified to tried.
    EbSimTickCostMeter clampCase{};
    clampCase.notePlanOutcome(false, 2, 99);
    expect(std::fabs(clampCase.fullyVerifiedRate() - 1.f) < 1e-4f, "full-clamp");

    if (fails == 0)
        report = "SimTickCostMeter OK";
    else
        report = fmt::format("SimTickCostMeter {} fails: {}", fails, report);
    return fails == 0;
}
