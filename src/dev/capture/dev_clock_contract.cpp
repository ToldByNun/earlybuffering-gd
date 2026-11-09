#include "../../app/class_manager.hpp"
#include "dev_clock_contract.hpp"
#include "../../core/assist/assist_clock.hpp"
#include <cmath>
#include <cstring>
#include <fmt/format.h>

EbDevClockContract& EbDevClockContract::get() {
    return EbClassManager::get().service<EbDevClockContract>();
}

char const* EbDevClockContract::simPathId(DevSimPathId id) {
    switch (id) {
    case DevSimPathId::Plan:
        return "ShadowSimulator.plan";
    case DevSimPathId::PlanSequence:
        return "ShadowSimulator.planSequence";
    case DevSimPathId::VerifyDelay:
        return "ShadowSimulator.verifyDelay";
    case DevSimPathId::EvalDelayFromFreeze:
        return "ShadowSimulator.evalDelayFromFreeze";
    case DevSimPathId::ReplayActualFire:
        return "ShadowSimulator.replayActualFire";
    case DevSimPathId::None:
        break;
    }
    return "none";
}

bool EbDevClockContract::parseSimPathId(std::string const& s, DevSimPathId& out) {
    if (s == "none" || s.empty()) {
        out = DevSimPathId::None;
        return true;
    }
    if (s == "ShadowSimulator.plan") {
        out = DevSimPathId::Plan;
        return true;
    }
    if (s == "ShadowSimulator.planSequence") {
        out = DevSimPathId::PlanSequence;
        return true;
    }
    if (s == "ShadowSimulator.verifyDelay") {
        out = DevSimPathId::VerifyDelay;
        return true;
    }
    if (s == "ShadowSimulator.evalDelayFromFreeze") {
        out = DevSimPathId::EvalDelayFromFreeze;
        return true;
    }
    if (s == "ShadowSimulator.replayActualFire") {
        out = DevSimPathId::ReplayActualFire;
        return true;
    }
    return false;
}

char const* EbDevClockContract::rejectId(Reject result) {
    switch (result) {
    case Reject::Ok:
        return "ok";
    case Reject::UnknownSchema:
        return "unknownSchema";
    case Reject::BadTimeBasis:
        return "badTimeBasis";
    case Reject::BadSimDt:
        return "badSimDt";
    case Reject::BadFirstDt:
        return "badFirstDt";
    case Reject::EffectTickMismatch:
        return "effectTickMismatch";
    }
    return "unknown";
}

int EbDevClockContract::effectTick(int attemptTick, bool midStep, bool continuous) const {
    if (attemptTick < 0)
        attemptTick = 0;
    if (!(continuous)) {
        return midStep ? attemptTick : attemptTick + 1;
    }
    return attemptTick;
}

float EbDevClockContract::step0Dt(Stamp const& stamp) const {
    float const simDt = stamp.simDt;
    if (simDt <= 0.f)
        return 0.f;
    if (stamp.midStep && stamp.firstDt > 0.f && stamp.firstDt <= simDt) {
        return stamp.firstDt;
    }
    return simDt;
}

std::vector<float> EbDevClockContract::stepSchedule(Stamp const& stamp, int delay,
                                                  int horizonTicks) const {
    std::vector<float> steps;
    if (delay < 0)
        delay = 0;
    if (horizonTicks < 0)
        horizonTicks = 0;
    int const count = delay + horizonTicks;
    if (count <= 0)
        return steps;
    if (stamp.simDt <= 0.f)
        return steps;

    steps.resize(static_cast<size_t>(count));
    float const s0 = this->step0Dt(stamp);
    float const rest = stamp.simDt;
    steps[0] = s0;
    for (int index = 1; index < count; ++index) {
        steps[static_cast<size_t>(index)] = rest;
    }
    return steps;
}

uint64_t EbDevClockContract::hashSteps(std::vector<float> const& steps) const {
    uint64_t height = 14695981039346656037ull;
    auto mix = [&](uint64_t value) {
        height ^= value;
        height *= 1099511628211ull;
    };
    mix(static_cast<uint64_t>(steps.size()));
    for (float deltaTime : steps) {
        // Quantize to microseconds so float noise does not flip the hash.
        long long const elapsedMicroseconds =
            static_cast<long long>(std::llround(static_cast<double>(deltaTime) * 1.0e6));
        mix(static_cast<uint64_t>(elapsedMicroseconds));
    }
    return height;
}

uint64_t EbDevClockContract::stepScheduleHash() const {
    // Reference table — must stay in sync with selfCheck cases.
    class EbClockScheduleRow {
    public:
        bool midStep;
        float firstDt;
        float simDt;
        int delay;
        int horizon;

    private:
    };
    EbClockScheduleRow const rows[] = {
        {false, 1.f / 240.f, 1.f / 240.f, 0, 4},
        {false, 1.f / 240.f, 1.f / 240.f, 2, 8},
        {true, 1.f / 480.f, 1.f / 240.f, 0, 4},
        {true, 1.f / 480.f, 1.f / 240.f, 3, 6},
        {true, 0.f, 1.f / 240.f, 1, 2},
        {true, 1.f / 100.f, 1.f / 240.f, 1, 2}, // firstDt > simDt → clamp to simDt
        {false, 1.f / 500.f, 1.f / 240.f, 5, 1},
    };

    uint64_t height = 14695981039346656037ull;
    auto mix = [&](uint64_t value) {
        height ^= value;
        height *= 1099511628211ull;
    };
    mix(static_cast<uint64_t>(EbDevClockContract::kSchema));
    for (auto const& row : rows) {
        Stamp st;
        st.simDt = row.simDt;
        st.firstDt = row.firstDt;
        st.midStep = row.midStep;
        st.continuous = false;
        st.attemptTick = 10;
        st.effectTick = this->effectTick(10, row.midStep, false);
        st.valid = true;
        auto steps = this->stepSchedule(st, row.delay, row.horizon);
        mix(this->hashSteps(steps));
        mix(static_cast<uint64_t>(st.effectTick));
        mix(row.midStep ? 1ull : 0ull);
        mix(static_cast<uint64_t>(row.delay));
        mix(static_cast<uint64_t>(row.horizon));
    }
    return height;
}

EbDevClockContract::Stamp EbDevClockContract::makeStamp(int attemptTick, bool midStep, bool continuous,
                                                    float simDt, float firstDt, float wallDt,
                                                    float timeWarp, DevSimPathId livePath) const {
    Stamp st;
    st.clockSchema = EbDevClockContract::kSchema;
    st.timeBasis = EbDevClockContract::kTimeBasisPhysicsTick;
    st.attemptTick = attemptTick < 0 ? 0 : attemptTick;
    st.midStep = midStep;
    st.continuous = continuous;
    st.simDt = simDt;
    st.firstDt = firstDt;
    st.wallDt = wallDt;
    st.timeWarp = timeWarp > 0.f ? timeWarp : 1.f;
    st.simTps = EbAssistClock::get().simTpsFromDt(simDt);
    st.speedScaleSuspect = EbAssistClock::get().speedScaleSuspect(st.timeWarp, st.simTps);
    st.effectTick = this->effectTick(st.attemptTick, midStep, continuous);
    st.liveSimPathId = livePath;
    st.valid = true;
    return st;
}

EbDevClockContract::Reject EbDevClockContract::validate(Stamp const& stamp) const {
    if (stamp.clockSchema != EbDevClockContract::kSchema) {
        return Reject::UnknownSchema;
    }
    if (stamp.timeBasis != EbDevClockContract::kTimeBasisPhysicsTick) {
        return Reject::BadTimeBasis;
    }
    if (!(stamp.simDt > 0.f) || !std::isfinite(stamp.simDt)) {
        return Reject::BadSimDt;
    }
    if (stamp.midStep) {
        if (!(stamp.firstDt > 0.f) || !(stamp.firstDt <= stamp.simDt) ||
            !std::isfinite(stamp.firstDt)) {
            return Reject::BadFirstDt;
        }
    }
    int const expect = this->effectTick(stamp.attemptTick, stamp.midStep, stamp.continuous);
    if (stamp.effectTick != expect) {
        return Reject::EffectTickMismatch;
    }
    return Reject::Ok;
}

bool EbDevClockContract::selfCheck(std::string& report) {
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

    EbDevClockContract& clock = EbDevClockContract::get();

    expect(clock.effectTick(10, true, false) == 10, "eff-discrete-mid");
    expect(clock.effectTick(10, false, false) == 11, "eff-discrete-between");
    expect(clock.effectTick(10, true, true) == 10, "eff-cont-mid");
    expect(clock.effectTick(10, false, true) == 10, "eff-cont-between");
    expect(clock.effectTick(-3, false, false) == 1, "eff-neg-discrete");
    expect(clock.effectTick(-3, true, true) == 0, "eff-neg-cont");

    // midStep + CBF remainder → step0 = firstDt; rest = simDt.
    {
        Stamp state = clock.makeStamp(10, true, false, 1.f / 240.f, 1.f / 480.f, 1.f / 240.f, 1.f,
                                      DevSimPathId::Plan);
        expect(clock.clockOk(state), "stamp-mid-ok");
        expect(state.effectTick == 10, "stamp-mid-effect");
        auto steps = clock.stepSchedule(state, 2, 4);
        expect(steps.size() == 6, "sched-len-mid");
        if (steps.size() == 6) {
            expect(std::fabs(steps[0] - 1.f / 480.f) < 1e-9f, "sched-s0-firstDt");
            expect(std::fabs(steps[1] - 1.f / 240.f) < 1e-9f, "sched-s1-simDt");
            expect(std::fabs(steps[5] - 1.f / 240.f) < 1e-9f, "sched-tail-simDt");
        }
    }

    // Between frames: firstDt ignored even if smaller — step0 = simDt.
    {
        Stamp state = clock.makeStamp(10, false, false, 1.f / 240.f, 1.f / 480.f, 1.f / 240.f, 1.f,
                                      DevSimPathId::PlanSequence);
        expect(state.effectTick == 11, "stamp-between-effect");
        expect(clock.clockOk(state), "stamp-between-ok");
        auto steps = clock.stepSchedule(state, 0, 3);
        expect(steps.size() == 3, "sched-len-between");
        if (!steps.empty()) {
            expect(std::fabs(steps[0] - 1.f / 240.f) < 1e-9f, "sched-between-ignores-firstDt");
        }
    }

    // Continuous between-frames: effectTick does not +1.
    {
        Stamp state = clock.makeStamp(42, false, true, 1.f / 240.f, 1.f / 240.f, 1.f / 240.f, 1.f,
                                      DevSimPathId::None);
        expect(state.effectTick == 42, "stamp-cont-between");
        expect(clock.clockOk(state), "stamp-cont-ok");
    }

    // midStep with firstDt out of band → BadFirstDt.
    {
        Stamp state = clock.makeStamp(5, true, false, 1.f / 240.f, 1.f / 100.f, 1.f / 240.f, 1.f,
                                      DevSimPathId::Plan);
        expect(clock.validate(state) == Reject::BadFirstDt, "validate-bad-firstDt");
        // stepSchedule still clamps step0 to simDt (safe fallback).
        auto steps = clock.stepSchedule(state, 1, 1);
        expect(steps.size() == 2, "sched-bad-first-len");
        if (!steps.empty()) {
            expect(std::fabs(steps[0] - 1.f / 240.f) < 1e-9f, "sched-bad-first-clamps");
        }
    }

    // Schema / timeBasis / effectTick mismatch.
    {
        Stamp state = clock.makeStamp(8, true, false, 1.f / 240.f, 1.f / 240.f, 1.f / 240.f, 1.f,
                                      DevSimPathId::Plan);
        state.clockSchema = 99;
        expect(clock.validate(state) == Reject::UnknownSchema, "validate-schema");
        state = clock.makeStamp(8, true, false, 1.f / 240.f, 1.f / 240.f, 1.f / 240.f, 1.f,
                                DevSimPathId::Plan);
        state.timeBasis = "wallClock";
        expect(clock.validate(state) == Reject::BadTimeBasis, "validate-basis");
        state = clock.makeStamp(8, true, false, 1.f / 240.f, 1.f / 240.f, 1.f / 240.f, 1.f,
                                DevSimPathId::Plan);
        state.effectTick = 999;
        expect(clock.validate(state) == Reject::EffectTickMismatch, "validate-effect-mismatch");
        state = clock.makeStamp(8, false, false, 0.f, 0.f, 0.f, 1.f, DevSimPathId::Plan);
        expect(clock.validate(state) == Reject::BadSimDt, "validate-simDt");
    }

    // timeWarp / wallDt must not change the schedule (simDt already stripped).
    {
        Stamp firstValue = clock.makeStamp(10, true, false, 1.f / 240.f, 1.f / 480.f, 1.f / 120.f,
                                           2.f, DevSimPathId::Plan);
        Stamp secondValue = firstValue;
        secondValue.timeWarp = 1.f;
        secondValue.wallDt = 1.f / 240.f;
        secondValue.speedScaleSuspect = true;
        auto sa = clock.stepSchedule(firstValue, 2, 5);
        auto sb = clock.stepSchedule(secondValue, 2, 5);
        expect(sa.size() == sb.size(), "warp-annot-len");
        bool same = sa.size() == sb.size();
        for (size_t index = 0; same && index < sa.size(); ++index) {
            same = std::fabs(sa[index] - sb[index]) < 1e-9f;
        }
        expect(same, "warp-annot-ignored");
    }

    expect(std::strcmp(simPathId(DevSimPathId::Plan), "ShadowSimulator.plan") == 0, "path-plan");
    expect(std::strcmp(simPathId(DevSimPathId::EvalDelayFromFreeze),
                       "ShadowSimulator.evalDelayFromFreeze") == 0,
           "path-eval");
    DevSimPathId parsed = DevSimPathId::None;
    expect(parseSimPathId("ShadowSimulator.planSequence", parsed) &&
               parsed == DevSimPathId::PlanSequence,
           "path-parse");

    uint64_t const h1 = clock.stepScheduleHash();
    uint64_t const h2 = clock.stepScheduleHash();
    expect(h1 == h2, "sched-hash-stable");
    expect(h1 != 0, "sched-hash-nonzero");

    if (fails == 0) {
        report = fmt::format("DevClockContract selfCheck OK hash={:016x}", h1);
        return true;
    }
    report = std::string("DevClockContract selfCheck fails: ") + report;
    return false;
}
