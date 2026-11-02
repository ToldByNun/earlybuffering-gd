#include "LongForesight.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

int LongForesight::extendHorizon(
    int currentHorizon, int pressDeathTick, int maxHorizon
) {
    if (pressDeathTick < 0) return 0;
    if (currentHorizon <= 0 || maxHorizon <= currentHorizon) return 0;
    // Death at the trailing edge — longer look rarely changes the press story.
    if (pressDeathTick >= currentHorizon - kDeathEdgeSlop) return 0;
    int const step = std::max(kExtendStep, currentHorizon / 2);
    int const next = std::min(maxHorizon, currentHorizon + step);
    return next > currentHorizon ? next : 0;
}

bool LongForesight::idleLooksSafer(PathStats const& press, PathStats const& idle) {
    // Idle must look better than pressing/releasing now.
    if (!press.died) return false;
    if (!idle.died) return true;
    if (idle.deathTick < 0 || press.deathTick < 0) return !idle.died;
    if (idle.deathTick > press.deathTick) return true;
    if (idle.deathTick == press.deathTick
        && idle.endX > press.endX + kEndXSlop) {
        return true;
    }
    return false;
}

LongForesight::Result LongForesight::decide(Input const& in) {
    Result r;
    PathStats const* press = &in.press;
    PathStats const* idle = &in.idle;
    r.horizonUsed = std::max(0, in.horizon);

    if (in.hasExtended && in.horizonExt > in.horizon) {
        r.extended = true;
        r.horizonUsed = in.horizonExt;
        press = &in.pressExt;
        idle = &in.idleExt;
    }

    r.pressSurvived = !press->died;
    r.idleSurvived = !idle->died;
    r.idleSafer = idleLooksSafer(*press, *idle);
    r.needDelaySearch = press->died;

    if (!r.idleSafer) {
        if (!press->died && idle->died) {
            r.pressBetter = true;
        }
        else if (press->died && idle->died
            && press->deathTick > idle->deathTick) {
            r.pressBetter = true;
        }
        else if (!press->died && !idle->died
            && press->endX > idle->endX + kEndXSlop) {
            r.pressBetter = true;
        }
    }
    return r;
}

bool LongForesight::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    // Press dies mid-window → extend toward max.
    expect(extendHorizon(64, 20, 160) > 64, "extend-mid");
    expect(extendHorizon(64, 20, 160) <= 160, "extend-cap");
    // Alive → no extend.
    expect(extendHorizon(64, -1, 160) == 0, "extend-alive");
    // Death at trailing edge → no extend.
    expect(extendHorizon(64, 62, 160) == 0, "extend-edge");
    // Already at max → no extend.
    expect(extendHorizon(160, 20, 160) == 0, "extend-at-max");

    PathStats pressDead{};
    pressDead.died = true;
    pressDead.deathTick = 10;
    pressDead.endX = 100.f;
    pressDead.surviveTicks = 10;

    PathStats idleLive{};
    idleLive.died = false;
    idleLive.endX = 200.f;
    idleLive.surviveTicks = 64;

    expect(idleLooksSafer(pressDead, idleLive), "idle-live-safer");
    expect(!idleLooksSafer(idleLive, pressDead), "press-live-not-idle");

    PathStats idleLater = pressDead;
    idleLater.deathTick = 40;
    idleLater.endX = 180.f;
    expect(idleLooksSafer(pressDead, idleLater), "idle-later-safer");

    PathStats idleWorse = pressDead;
    idleWorse.deathTick = 5;
    idleWorse.endX = 50.f;
    expect(!idleLooksSafer(pressDead, idleWorse), "idle-worse");

    // decide: press dies, idle lives → idleSafer + needDelaySearch
    {
        Input in;
        in.press = pressDead;
        in.idle = idleLive;
        in.horizon = 64;
        auto const r = decide(in);
        expect(r.idleSafer && r.needDelaySearch && !r.pressSurvived, "decide-idle");
        expect(r.idleSurvived && !r.pressBetter, "decide-idle-flags");
        expect(r.horizonUsed == 64 && !r.extended, "decide-base-h");
    }

    // decide: press survives → no delay search
    {
        Input in;
        in.press = idleLive;
        in.idle = pressDead;
        in.horizon = 64;
        auto const r = decide(in);
        expect(!r.needDelaySearch && r.pressSurvived && r.pressBetter, "decide-press");
        expect(!r.idleSafer, "decide-press-not-idle");
    }

    // Extended pass: base press dies, extended press lives.
    {
        Input in;
        in.press = pressDead;
        in.idle = pressDead;
        in.horizon = 64;
        in.hasExtended = true;
        in.horizonExt = 128;
        in.pressExt = idleLive;
        in.idleExt = pressDead;
        auto const r = decide(in);
        expect(r.extended && r.horizonUsed == 128, "decide-ext-h");
        expect(!r.needDelaySearch && r.pressSurvived && r.pressBetter, "decide-ext-ok");
    }

    // Extended still dies → needDelaySearch
    {
        Input in;
        in.press = pressDead;
        in.idle = idleLive;
        in.horizon = 64;
        in.hasExtended = true;
        in.horizonExt = 128;
        in.pressExt = pressDead;
        in.idleExt = idleLive;
        auto const r = decide(in);
        expect(r.needDelaySearch && r.idleSafer && r.extended, "decide-ext-still-dead");
    }

    if (fails == 0) {
        report = "LongForesight selfCheck OK";
        return true;
    }
    report = fmt::format("LongForesight selfCheck {} fails: {}", fails, report);
    return false;
}
