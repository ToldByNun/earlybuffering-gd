#include "AssistClock.hpp"

#include <cmath>
#include <cstdlib>

AssistClock& AssistClock::get() {
    static AssistClock instance;
    return instance;
}

double AssistClock::simTpsFromDt(float simDt) const {
    if (simDt <= 0.f) return AssistClock::kNominalTps;
    return 1.0 / static_cast<double>(simDt);
}

double AssistClock::wallTpsFromDt(float lastDt) const {
    if (lastDt <= 0.f) return AssistClock::kNominalTps;
    return 1.0 / static_cast<double>(lastDt);
}

bool AssistClock::isSaneSimDt(float simDt) const {
    if (!std::isfinite(simDt)) return false;
    return simDt >= AssistClock::kMinSaneSimDt && simDt <= AssistClock::kMaxSaneSimDt;
}

float AssistClock::planSimDtOrNominal(float simDt, float timeWarp) const {
    float tw = timeWarp;
    if (tw <= 0.f) tw = 1.f;
    if (!this->isSaneSimDt(simDt)) {
        return AssistClock::kNominalSimDt;
    }
    double const tps = this->simTpsFromDt(simDt);
    // Extreme stuck hitch (e.g. lastDt poisoned to 0.25 → 4 TPS) — never plan with it.
    if (this->speedScaleSuspect(tw, tps)
        && (tps < AssistClock::kPlanSnapTpsLo || tps > AssistClock::kPlanSnapTpsHi)) {
        return AssistClock::kNominalSimDt;
    }
    return simDt;
}

int AssistClock::framesToTicks(int frames) {
    if (frames <= 0) return 0;
    double const ticks = static_cast<double>(frames)
        * (AssistClock::kNominalTps / AssistClock::kDisplayFps);
    long long const rounded = static_cast<long long>(std::llround(ticks));
    if (rounded < 0) return 0;
    if (rounded > 0x7fffffff) return 0x7fffffff;
    return static_cast<int>(rounded);
}

int AssistClock::ticksToFrames(int ticks) {
    if (ticks <= 0) return 0;
    double const frames = static_cast<double>(ticks)
        * (AssistClock::kDisplayFps / AssistClock::kNominalTps);
    long long const rounded = static_cast<long long>(std::llround(frames));
    if (rounded < 0) return 0;
    if (rounded > 0x7fffffff) return 0x7fffffff;
    return static_cast<int>(rounded);
}

float AssistClock::gdStepToSeconds(float gdDt) {
    if (!(gdDt > 0.f) || !std::isfinite(gdDt)) return 0.f;
    // GD passes 60Hz-normalized step units (60/240 = 0.25 at 240 TPS) to
    // PlayerObject::update. If a caller already passed seconds (<= 1/15s), keep it.
    if (gdDt > AssistClock::kMaxSaneSimDt) {
        return gdDt / static_cast<float>(AssistClock::kDisplayFps);
    }
    return gdDt;
}

float AssistClock::secondsToGdStep(float secDt) {
    if (!(secDt > 0.f) || !std::isfinite(secDt)) {
        return AssistClock::kNominalSimDt * static_cast<float>(AssistClock::kDisplayFps);
    }
    // If secDt is in seconds (<= 1/15s), scale by 60Hz for PlayerObject::update / checkCollisions.
    if (secDt <= AssistClock::kMaxSaneSimDt * 2.f) {
        return secDt * static_cast<float>(AssistClock::kDisplayFps);
    }
    return secDt;
}

int AssistClock::remapTick(double authorTps, int liveAbsTick, float liveSimDt) const {
    if (liveAbsTick < 0) return 0;
    if (authorTps <= 0.0) authorTps = AssistClock::kNominalTps;
    if (liveSimDt <= 0.f) {
        return liveAbsTick;
    }
    double const scale = authorTps * static_cast<double>(liveSimDt);
    if (!std::isfinite(scale) || scale <= 0.0) {
        return liveAbsTick;
    }
    long long const mapped = static_cast<long long>(
        std::llround(static_cast<double>(liveAbsTick) * scale)
    );
    if (mapped < 0) return 0;
    if (mapped > static_cast<long long>(0x7fffffff)) return 0x7fffffff;
    return static_cast<int>(mapped);
}

bool AssistClock::speedScaleSuspect(float timeWarp, double simTps) const {
    float tw = timeWarp;
    if (tw <= 0.f) tw = 1.f;
    if (std::fabs(tw - 1.f) > AssistClock::kWarpNearOne) {
        return false;
    }
    if (!std::isfinite(simTps) || simTps <= 0.0) {
        return false;
    }
    return std::fabs(simTps - AssistClock::kNominalTps) > AssistClock::kSuspectAbsDelta;
}

bool AssistClock::noteSpeedScale(float timeWarp, double simTps, int attemptTick) {
    bool const suspect = this->speedScaleSuspect(timeWarp, simTps);
    bool const edgeOn = suspect && !this->m_lastSuspect;
    bool const stickyPulse = suspect
        && (attemptTick - this->m_lastSuspectLogTick) >= AssistClock::kSuspectLogEveryTicks;
    this->m_lastSuspect = suspect;
    if (!edgeOn && !stickyPulse) {
        return false;
    }
    this->m_lastSuspectLogTick = attemptTick;
    return true;
}

bool AssistClock::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += ", ";
        report += name;
    };

    AssistClock& clock = AssistClock::get();

    expect(
        std::fabs(clock.simTpsFromDt(1.f / 240.f) - 240.0) < 0.01,
        "sim-tps-240"
    );
    expect(clock.simTpsFromDt(0.f) == AssistClock::kNominalTps, "sim-tps-zero");
    expect(clock.simTpsFromDt(-1.f) == AssistClock::kNominalTps, "sim-tps-neg");
    expect(clock.isSaneSimDt(AssistClock::kNominalSimDt), "sane-nominal");
    expect(!clock.isSaneSimDt(0.25f), "sane-reject-hitch");
    expect(
        std::fabs(clock.planSimDtOrNominal(0.25f, 1.f) - AssistClock::kNominalSimDt) < 1e-6f,
        "plan-snap-hitch"
    );
    expect(
        std::fabs(clock.planSimDtOrNominal(AssistClock::kNominalSimDt, 1.f)
            - AssistClock::kNominalSimDt) < 1e-6f,
        "plan-keep-nominal"
    );
    expect(AssistClock::framesToTicks(1) == 4, "frames-1-to-4-ticks");
    expect(AssistClock::framesToTicks(8) == 32, "frames-8-to-32-ticks");
    expect(AssistClock::ticksToFrames(4) == 1, "ticks-4-to-1-frame");
    expect(AssistClock::ticksToFrames(2) == 1 || AssistClock::ticksToFrames(2) == 0, "ticks-2-round");
    expect(
        std::fabs(AssistClock::gdStepToSeconds(0.25f) - AssistClock::kNominalSimDt) < 1e-6f,
        "gd-step-025-to-sec"
    );
    expect(
        std::fabs(AssistClock::secondsToGdStep(AssistClock::kNominalSimDt) - 0.25f) < 1e-6f,
        "sec-to-gd-step-025"
    );

    // lastDt = 1/120, tw = 2 → simDt = 1/240 → wall 120, sim 240.
    float const lastDt = 1.f / 120.f;
    float const simDt = lastDt / 2.f;
    expect(
        std::fabs(clock.wallTpsFromDt(lastDt) - 120.0) < 0.05,
        "wall-tps-120"
    );
    expect(
        std::fabs(clock.simTpsFromDt(simDt) - 240.0) < 0.05,
        "sim-tps-warp-strip"
    );

    expect(clock.remapTick(240.0, 100, 1.f / 240.f) == 100, "remap-identity");
    expect(clock.remapTick(240.0, 100, 1.f / 120.f) == 200, "remap-2x-dt");
    expect(clock.remapTick(120.0, 100, 1.f / 120.f) == 100, "remap-matched-author");
    expect(clock.remapTick(240.0, -5, 1.f / 240.f) == 0, "remap-neg-live");
    expect(clock.remapTick(0.0, 50, 1.f / 240.f) == 50, "remap-bad-author");

    expect(!clock.speedScaleSuspect(1.f, 240.0), "suspect-nominal");
    expect(!clock.speedScaleSuspect(1.f, 220.0), "suspect-within-band");
    expect(clock.speedScaleSuspect(1.f, 120.0), "suspect-half-speed");
    expect(clock.speedScaleSuspect(1.f, 360.0), "suspect-fast");
    expect(!clock.speedScaleSuspect(2.f, 120.0), "suspect-warp-explains");
    // tw<=0 treated as 1 → same as nominal-warp path (120 is suspect).
    expect(clock.speedScaleSuspect(0.f, 120.0), "suspect-bad-warp-as-one");

    // noteSpeedScale: edge then throttle.
    clock.m_lastSuspect = false;
    clock.m_lastSuspectLogTick = -1000000;
    expect(clock.noteSpeedScale(1.f, 120.0, 10), "note-edge-on");
    expect(clock.lastSuspect(), "note-sticky");
    expect(!clock.noteSpeedScale(1.f, 120.0, 11), "note-throttle");
    expect(
        clock.noteSpeedScale(1.f, 120.0, 10 + AssistClock::kSuspectLogEveryTicks),
        "note-pulse"
    );
    expect(!clock.noteSpeedScale(1.f, 240.0, 500), "note-clear-no-log");
    expect(!clock.lastSuspect(), "note-cleared");

    // Leave singleton clean for live play after PlanGold.
    clock.m_lastSuspect = false;
    clock.m_lastSuspectLogTick = -1000000;

    if (fails == 0) {
        report = "AssistClock selfCheck OK";
        return true;
    }
    report = std::string("AssistClock selfCheck fails: ") + report;
    return false;
}
