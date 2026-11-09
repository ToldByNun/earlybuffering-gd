#include "../../app/class_manager.hpp"
#include "assist_clock.hpp"
#include <cmath>
#include <cstdlib>

EbAssistClock& EbAssistClock::get() {
    return EbClassManager::get().service<EbAssistClock>();
}

double EbAssistClock::simTpsFromDt(float simDt) const {
    if (!(simDt <= 0.f)) {
        return 1.0 / static_cast<double>(simDt);
    }
    return EbAssistClock::kNominalTps;
}

double EbAssistClock::wallTpsFromDt(float lastDt) const {
    if (!(lastDt <= 0.f)) {
        return 1.0 / static_cast<double>(lastDt);
    }
    return EbAssistClock::kNominalTps;
}

bool EbAssistClock::isSaneSimDt(float simDt) const {
    if (!(!std::isfinite(simDt))) {
        return simDt >= EbAssistClock::kMinSaneSimDt && simDt <= EbAssistClock::kMaxSaneSimDt;
    }
    return false;
}

float EbAssistClock::planSimDtOrNominal(float simDt, float timeWarp) const {
    float tw = timeWarp;
    if (tw <= 0.f)
        tw = 1.f;
    if (!this->isSaneSimDt(simDt)) {
        return EbAssistClock::kNominalSimDt;
    }
    double const tps = this->simTpsFromDt(simDt);
    // Extreme stuck hitch in *seconds* (e.g. multi-frame pause dump) — never plan with it.
    // Do NOT treat raw GD step 0.25 here; callers must gdStepToSeconds first.
    if (this->speedScaleSuspect(tw, tps) &&
        (tps < EbAssistClock::kPlanSnapTpsLo || tps > EbAssistClock::kPlanSnapTpsHi)) {
        return EbAssistClock::kNominalSimDt;
    }
    return simDt;
}

int EbAssistClock::framesToTicks(int frames) {
    if (frames <= 0)
        return 0;
    double const ticks =
        static_cast<double>(frames) * (EbAssistClock::kNominalTps / EbAssistClock::kDisplayFps);
    long long const rounded = static_cast<long long>(std::llround(ticks));
    if (rounded < 0)
        return 0;
    if (!(rounded > 0x7fffffff)) {
        return static_cast<int>(rounded);
    }
    return 0x7fffffff;
}

int EbAssistClock::ticksToFrames(int ticks) {
    if (ticks <= 0)
        return 0;
    double const frames =
        static_cast<double>(ticks) * (EbAssistClock::kDisplayFps / EbAssistClock::kNominalTps);
    long long const rounded = static_cast<long long>(std::llround(frames));
    if (rounded < 0)
        return 0;
    if (!(rounded > 0x7fffffff)) {
        return static_cast<int>(rounded);
    }
    return 0x7fffffff;
}

float EbAssistClock::gdStepToSeconds(float gdDt) {
    if (!(gdDt > 0.f) || !std::isfinite(gdDt))
        return 0.f;
    // GD step units are typically around kNominalGdStep (0.25). Real seconds
    // at 240 TPS are ~1/240. Anything above the seconds sane max is GD step.
    if (gdDt > EbAssistClock::kMaxSaneSimDt) {
        return gdDt / static_cast<float>(EbAssistClock::kDisplayFps);
    }
    return gdDt;
}

float EbAssistClock::secondsToGdStep(float secDt) {
    if (!(secDt > 0.f) || !std::isfinite(secDt)) {
        return EbAssistClock::kNominalGdStep;
    }
    // Seconds-domain planning quanta → GD PlayerObject::update step (1.2.1 contract).
    if (secDt <= EbAssistClock::kMaxSaneSimDt * 2.f) {
        return secDt * static_cast<float>(EbAssistClock::kDisplayFps);
    }
    return secDt;
}

int EbAssistClock::remapTick(double authorTps, int liveAbsTick, float liveSimDt) const {
    if (liveAbsTick < 0)
        return 0;
    if (authorTps <= 0.0)
        authorTps = EbAssistClock::kNominalTps;
    if (liveSimDt <= 0.f) {
        return liveAbsTick;
    }
    double const scale = authorTps * static_cast<double>(liveSimDt);
    if (!std::isfinite(scale) || scale <= 0.0) {
        return liveAbsTick;
    }
    long long const mapped =
        static_cast<long long>(std::llround(static_cast<double>(liveAbsTick) * scale));
    if (mapped < 0)
        return 0;
    if (!(mapped > static_cast<long long>(0x7fffffff))) {
        return static_cast<int>(mapped);
    }
    return 0x7fffffff;
}

int EbAssistClock::unmapTick(double authorTps, int authorTick, float liveSimDt) const {
    if (authorTick < 0)
        return 0;
    if (authorTps <= 0.0)
        authorTps = EbAssistClock::kNominalTps;
    if (liveSimDt <= 0.f) {
        return authorTick;
    }
    double const scale = authorTps * static_cast<double>(liveSimDt);
    if (!std::isfinite(scale) || scale <= 1e-12) {
        return authorTick;
    }
    long long const mapped =
        static_cast<long long>(std::llround(static_cast<double>(authorTick) / scale));
    if (mapped < 0)
        return 0;
    if (!(mapped > static_cast<long long>(0x7fffffff))) {
        return static_cast<int>(mapped);
    }
    return 0x7fffffff;
}

bool EbAssistClock::speedScaleSuspect(float timeWarp, double simTps) const {
    float tw = timeWarp;
    if (tw <= 0.f)
        tw = 1.f;
    if (std::fabs(tw - 1.f) > EbAssistClock::kWarpNearOne) {
        return false;
    }
    if (!std::isfinite(simTps) || simTps <= 0.0) {
        return false;
    }
    return std::fabs(simTps - EbAssistClock::kNominalTps) > EbAssistClock::kSuspectAbsDelta;
}

bool EbAssistClock::noteSpeedScale(float timeWarp, double simTps, int attemptTick) {
    bool const suspect = this->speedScaleSuspect(timeWarp, simTps);
    bool const edgeOn = suspect && !this->m_lastSuspect;
    bool const stickyPulse =
        suspect && (attemptTick - this->m_lastSuspectLogTick) >= EbAssistClock::kSuspectLogEveryTicks;
    this->m_lastSuspect = suspect;
    if (!edgeOn && !stickyPulse) {
        return false;
    }
    this->m_lastSuspectLogTick = attemptTick;
    return true;
}

bool EbAssistClock::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += ", ";
        report += name;
    };

    EbAssistClock& clock = EbAssistClock::get();

    expect(std::fabs(clock.simTpsFromDt(1.f / 240.f) - 240.0) < 0.01, "sim-tps-240");
    expect(clock.simTpsFromDt(0.f) == EbAssistClock::kNominalTps, "sim-tps-zero");
    expect(clock.simTpsFromDt(-1.f) == EbAssistClock::kNominalTps, "sim-tps-neg");
    expect(clock.isSaneSimDt(EbAssistClock::kNominalSimDt), "sane-nominal");
    // Raw GD step 0.25 is insane *as seconds* — must convert before sanity.
    expect(!clock.isSaneSimDt(EbAssistClock::kNominalGdStep), "sane-reject-raw-gd-step");
    expect(std::fabs(clock.planSimDtOrNominal(EbAssistClock::kNominalGdStep, 1.f) -
                     EbAssistClock::kNominalSimDt) < 1e-6f,
           "plan-snap-raw-gd-step");
    expect(std::fabs(EbAssistClock::kNominalGdStep - 0.25f) < 1e-6f, "nominal-gd-step-025");
    expect(std::fabs(clock.planSimDtOrNominal(EbAssistClock::kNominalSimDt, 1.f) -
                     EbAssistClock::kNominalSimDt) < 1e-6f,
           "plan-keep-nominal");
    expect(EbAssistClock::framesToTicks(1) == 4, "frames-1-to-4-ticks");
    expect(EbAssistClock::framesToTicks(8) == 32, "frames-8-to-32-ticks");
    expect(EbAssistClock::ticksToFrames(4) == 1, "ticks-4-to-1-frame");
    expect(EbAssistClock::ticksToFrames(2) == 1 || EbAssistClock::ticksToFrames(2) == 0,
           "ticks-2-round");
    expect(std::fabs(EbAssistClock::gdStepToSeconds(EbAssistClock::kNominalGdStep) -
                     EbAssistClock::kNominalSimDt) < 1e-6f,
           "gd-step-025-to-sec");
    expect(std::fabs(EbAssistClock::secondsToGdStep(EbAssistClock::kNominalSimDt) -
                     EbAssistClock::kNominalGdStep) < 1e-6f,
           "sec-to-gd-step-025");
    // Round-trip: live GD hook dt must equal ghost update dt (v1.2.1 contract).
    expect(std::fabs(EbAssistClock::secondsToGdStep(EbAssistClock::gdStepToSeconds(0.25f)) - 0.25f) <
               1e-6f,
           "gd-step-roundtrip");

    // lastDt = 1/120, tw = 2 → simDt = 1/240 → wall 120, sim 240.
    float const lastDt = 1.f / 120.f;
    float const simDt = lastDt / 2.f;
    expect(std::fabs(clock.wallTpsFromDt(lastDt) - 120.0) < 0.05, "wall-tps-120");
    expect(std::fabs(clock.simTpsFromDt(simDt) - 240.0) < 0.05, "sim-tps-warp-strip");

    expect(clock.remapTick(240.0, 100, 1.f / 240.f) == 100, "remap-identity");
    expect(clock.remapTick(240.0, 100, 1.f / 120.f) == 200, "remap-2x-dt");
    expect(clock.remapTick(120.0, 100, 1.f / 120.f) == 100, "remap-matched-author");
    expect(clock.remapTick(240.0, -5, 1.f / 240.f) == 0, "remap-neg-live");
    expect(clock.remapTick(0.0, 50, 1.f / 240.f) == 50, "remap-bad-author");
    // Replay @360 → game @240: frame 86 → round(86 * 240/360) = 57.
    expect(clock.remapTick(240.0, 86, 1.f / 360.f) == 57, "remap-360-to-240");
    expect(clock.unmapTick(240.0, 100, 1.f / 240.f) == 100, "unmap-identity");
    expect(clock.unmapTick(240.0, 200, 1.f / 120.f) == 100, "unmap-2x-dt");
    expect(clock.unmapTick(240.0, clock.remapTick(240.0, 86, 1.f / 360.f), 1.f / 360.f) == 86,
           "unmap-roundtrip-360");

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
    expect(clock.noteSpeedScale(1.f, 120.0, 10 + EbAssistClock::kSuspectLogEveryTicks), "note-pulse");
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
