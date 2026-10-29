#include "DevClockContract.hpp"

#include "../core/AssistClock.hpp"

#include <cmath>
#include <cstring>
#include <fmt/format.h>

DevClockContract& DevClockContract::get() {
    static DevClockContract inst;
    return inst;
}

char const* DevClockContract::simPathId(DevSimPathId id) {
    switch (id) {
        case DevSimPathId::Plan: return "ShadowSimulator.plan";
        case DevSimPathId::PlanSequence: return "ShadowSimulator.planSequence";
        case DevSimPathId::VerifyDelay: return "ShadowSimulator.verifyDelay";
        case DevSimPathId::EvalDelayFromFreeze:
            return "ShadowSimulator.evalDelayFromFreeze";
        case DevSimPathId::ReplayActualFire:
            return "ShadowSimulator.replayActualFire";
        case DevSimPathId::None: break;
    }
    return "none";
}

bool DevClockContract::parseSimPathId(std::string const& s, DevSimPathId& out) {
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

char const* DevClockContract::rejectId(Reject r) {
    switch (r) {
        case Reject::Ok: return "ok";
        case Reject::UnknownSchema: return "unknownSchema";
        case Reject::BadTimeBasis: return "badTimeBasis";
        case Reject::BadSimDt: return "badSimDt";
        case Reject::BadFirstDt: return "badFirstDt";
        case Reject::EffectTickMismatch: return "effectTickMismatch";
    }
    return "unknown";
}

int DevClockContract::effectTick(
    int attemptTick, bool midStep, bool continuous
) const {
    if (attemptTick < 0) attemptTick = 0;
    if (continuous) return attemptTick;
    return midStep ? attemptTick : attemptTick + 1;
}

float DevClockContract::step0Dt(Stamp const& stamp) const {
    float const simDt = stamp.simDt;
    if (simDt <= 0.f) return 0.f;
    if (stamp.midStep
        && stamp.firstDt > 0.f
        && stamp.firstDt <= simDt) {
        return stamp.firstDt;
    }
    return simDt;
}

std::vector<float> DevClockContract::stepSchedule(
    Stamp const& stamp, int delay, int horizonTicks
) const {
    std::vector<float> steps;
    if (delay < 0) delay = 0;
    if (horizonTicks < 0) horizonTicks = 0;
    int const n = delay + horizonTicks;
    if (n <= 0) return steps;
    if (stamp.simDt <= 0.f) return steps;

    steps.resize(static_cast<size_t>(n));
    float const s0 = this->step0Dt(stamp);
    float const rest = stamp.simDt;
    steps[0] = s0;
    for (int i = 1; i < n; ++i) {
        steps[static_cast<size_t>(i)] = rest;
    }
    return steps;
}

uint64_t DevClockContract::hashSteps(std::vector<float> const& steps) const {
    uint64_t h = 14695981039346656037ull;
    auto mix = [&](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mix(static_cast<uint64_t>(steps.size()));
    for (float dt : steps) {
        // Quantize to microseconds so float noise does not flip the hash.
        long long const us = static_cast<long long>(
            std::llround(static_cast<double>(dt) * 1.0e6)
        );
        mix(static_cast<uint64_t>(us));
    }
    return h;
}

uint64_t DevClockContract::stepScheduleHash() const {
    // Reference table — must stay in sync with selfCheck cases.
    struct Row {
        bool midStep;
        float firstDt;
        float simDt;
        int delay;
        int horizon;
    };
    Row const rows[] = {
        {false, 1.f / 240.f, 1.f / 240.f, 0, 4},
        {false, 1.f / 240.f, 1.f / 240.f, 2, 8},
        {true, 1.f / 480.f, 1.f / 240.f, 0, 4},
        {true, 1.f / 480.f, 1.f / 240.f, 3, 6},
        {true, 0.f, 1.f / 240.f, 1, 2},
        {true, 1.f / 100.f, 1.f / 240.f, 1, 2}, // firstDt > simDt → clamp to simDt
        {false, 1.f / 500.f, 1.f / 240.f, 5, 1},
    };

    uint64_t h = 14695981039346656037ull;
    auto mix = [&](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mix(static_cast<uint64_t>(DevClockContract::kSchema));
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
    return h;
}

DevClockContract::Stamp DevClockContract::makeStamp(
    int attemptTick,
    bool midStep,
    bool continuous,
    float simDt,
    float firstDt,
    float wallDt,
    float timeWarp,
    DevSimPathId livePath
) const {
    Stamp st;
    st.clockSchema = DevClockContract::kSchema;
    st.timeBasis = DevClockContract::kTimeBasisPhysicsTick;
    st.attemptTick = attemptTick < 0 ? 0 : attemptTick;
    st.midStep = midStep;
    st.continuous = continuous;
    st.simDt = simDt;
    st.firstDt = firstDt;
    st.wallDt = wallDt;
    st.timeWarp = timeWarp > 0.f ? timeWarp : 1.f;
    st.simTps = AssistClock::get().simTpsFromDt(simDt);
    st.speedScaleSuspect = AssistClock::get().speedScaleSuspect(
        st.timeWarp, st.simTps
    );
    st.effectTick = this->effectTick(st.attemptTick, midStep, continuous);
    st.liveSimPathId = livePath;
    st.valid = true;
    return st;
}

DevClockContract::Reject DevClockContract::validate(Stamp const& stamp) const {
    if (stamp.clockSchema != DevClockContract::kSchema) {
        return Reject::UnknownSchema;
    }
    if (stamp.timeBasis != DevClockContract::kTimeBasisPhysicsTick) {
        return Reject::BadTimeBasis;
    }
    if (!(stamp.simDt > 0.f) || !std::isfinite(stamp.simDt)) {
        return Reject::BadSimDt;
    }
    if (stamp.midStep) {
        if (!(stamp.firstDt > 0.f)
            || !(stamp.firstDt <= stamp.simDt)
            || !std::isfinite(stamp.firstDt)) {
            return Reject::BadFirstDt;
        }
    }
    int const expect = this->effectTick(
        stamp.attemptTick, stamp.midStep, stamp.continuous
    );
    if (stamp.effectTick != expect) {
        return Reject::EffectTickMismatch;
    }
    return Reject::Ok;
}

bool DevClockContract::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += ", ";
        report += name;
    };

    DevClockContract& clock = DevClockContract::get();

    expect(clock.effectTick(10, true, false) == 10, "eff-discrete-mid");
    expect(clock.effectTick(10, false, false) == 11, "eff-discrete-between");
    expect(clock.effectTick(10, true, true) == 10, "eff-cont-mid");
    expect(clock.effectTick(10, false, true) == 10, "eff-cont-between");
    expect(clock.effectTick(-3, false, false) == 1, "eff-neg-discrete");
    expect(clock.effectTick(-3, true, true) == 0, "eff-neg-cont");

    // midStep + CBF remainder → step0 = firstDt; rest = simDt.
    {
        Stamp st = clock.makeStamp(
            10, true, false, 1.f / 240.f, 1.f / 480.f, 1.f / 240.f, 1.f,
            DevSimPathId::Plan
        );
        expect(clock.clockOk(st), "stamp-mid-ok");
        expect(st.effectTick == 10, "stamp-mid-effect");
        auto steps = clock.stepSchedule(st, 2, 4);
        expect(steps.size() == 6, "sched-len-mid");
        if (steps.size() == 6) {
            expect(
                std::fabs(steps[0] - 1.f / 480.f) < 1e-9f, "sched-s0-firstDt"
            );
            expect(
                std::fabs(steps[1] - 1.f / 240.f) < 1e-9f, "sched-s1-simDt"
            );
            expect(
                std::fabs(steps[5] - 1.f / 240.f) < 1e-9f, "sched-tail-simDt"
            );
        }
    }

    // Between frames: firstDt ignored even if smaller — step0 = simDt.
    {
        Stamp st = clock.makeStamp(
            10, false, false, 1.f / 240.f, 1.f / 480.f, 1.f / 240.f, 1.f,
            DevSimPathId::PlanSequence
        );
        expect(st.effectTick == 11, "stamp-between-effect");
        expect(clock.clockOk(st), "stamp-between-ok");
        auto steps = clock.stepSchedule(st, 0, 3);
        expect(steps.size() == 3, "sched-len-between");
        if (!steps.empty()) {
            expect(
                std::fabs(steps[0] - 1.f / 240.f) < 1e-9f,
                "sched-between-ignores-firstDt"
            );
        }
    }

    // Continuous between-frames: effectTick does not +1.
    {
        Stamp st = clock.makeStamp(
            42, false, true, 1.f / 240.f, 1.f / 240.f, 1.f / 240.f, 1.f,
            DevSimPathId::None
        );
        expect(st.effectTick == 42, "stamp-cont-between");
        expect(clock.clockOk(st), "stamp-cont-ok");
    }

    // midStep with firstDt out of band → BadFirstDt.
    {
        Stamp st = clock.makeStamp(
            5, true, false, 1.f / 240.f, 1.f / 100.f, 1.f / 240.f, 1.f,
            DevSimPathId::Plan
        );
        expect(
            clock.validate(st) == Reject::BadFirstDt, "validate-bad-firstDt"
        );
        // stepSchedule still clamps step0 to simDt (safe fallback).
        auto steps = clock.stepSchedule(st, 1, 1);
        expect(steps.size() == 2, "sched-bad-first-len");
        if (!steps.empty()) {
            expect(
                std::fabs(steps[0] - 1.f / 240.f) < 1e-9f,
                "sched-bad-first-clamps"
            );
        }
    }

    // Schema / timeBasis / effectTick mismatch.
    {
        Stamp st = clock.makeStamp(
            8, true, false, 1.f / 240.f, 1.f / 240.f, 1.f / 240.f, 1.f,
            DevSimPathId::Plan
        );
        st.clockSchema = 99;
        expect(
            clock.validate(st) == Reject::UnknownSchema, "validate-schema"
        );
        st = clock.makeStamp(
            8, true, false, 1.f / 240.f, 1.f / 240.f, 1.f / 240.f, 1.f,
            DevSimPathId::Plan
        );
        st.timeBasis = "wallClock";
        expect(
            clock.validate(st) == Reject::BadTimeBasis, "validate-basis"
        );
        st = clock.makeStamp(
            8, true, false, 1.f / 240.f, 1.f / 240.f, 1.f / 240.f, 1.f,
            DevSimPathId::Plan
        );
        st.effectTick = 999;
        expect(
            clock.validate(st) == Reject::EffectTickMismatch,
            "validate-effect-mismatch"
        );
        st = clock.makeStamp(
            8, false, false, 0.f, 0.f, 0.f, 1.f, DevSimPathId::Plan
        );
        expect(clock.validate(st) == Reject::BadSimDt, "validate-simDt");
    }

    // timeWarp / wallDt must not change the schedule (simDt already stripped).
    {
        Stamp a = clock.makeStamp(
            10, true, false, 1.f / 240.f, 1.f / 480.f, 1.f / 120.f, 2.f,
            DevSimPathId::Plan
        );
        Stamp b = a;
        b.timeWarp = 1.f;
        b.wallDt = 1.f / 240.f;
        b.speedScaleSuspect = true;
        auto sa = clock.stepSchedule(a, 2, 5);
        auto sb = clock.stepSchedule(b, 2, 5);
        expect(sa.size() == sb.size(), "warp-annot-len");
        bool same = sa.size() == sb.size();
        for (size_t i = 0; same && i < sa.size(); ++i) {
            same = std::fabs(sa[i] - sb[i]) < 1e-9f;
        }
        expect(same, "warp-annot-ignored");
    }

    expect(
        std::strcmp(simPathId(DevSimPathId::Plan), "ShadowSimulator.plan") == 0,
        "path-plan"
    );
    expect(
        std::strcmp(
            simPathId(DevSimPathId::EvalDelayFromFreeze),
            "ShadowSimulator.evalDelayFromFreeze"
        ) == 0,
        "path-eval"
    );
    DevSimPathId parsed = DevSimPathId::None;
    expect(
        parseSimPathId("ShadowSimulator.planSequence", parsed)
            && parsed == DevSimPathId::PlanSequence,
        "path-parse"
    );

    uint64_t const h1 = clock.stepScheduleHash();
    uint64_t const h2 = clock.stepScheduleHash();
    expect(h1 == h2, "sched-hash-stable");
    expect(h1 != 0, "sched-hash-nonzero");

    if (fails == 0) {
        report = fmt::format(
            "DevClockContract selfCheck OK hash={:016x}", h1
        );
        return true;
    }
    report = std::string("DevClockContract selfCheck fails: ") + report;
    return false;
}
