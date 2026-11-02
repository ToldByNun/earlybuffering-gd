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

bool LongForesight::pathCoversHorizon(PathStats const& s, int horizon) {
    if (horizon <= 0) return false;
    if (s.died) {
        return s.deathTick >= 0 && s.deathTick < horizon;
    }
    return s.surviveTicks >= horizon;
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
    r.bothDead = press->died && idle->died;

    // Escape-shaped only: press dies and idle still lives on this horizon.
    // Both-dead (even with a later idle deathTick / endX) must not force
    // Delay search or invent idleSafer — that needs an Alive later cand.
    bool const idleEscape = press->died && r.idleSurvived;
    r.idleSafer = idleEscape;
    r.needDelaySearch = idleEscape;

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

    expect(pathCoversHorizon(pressDead, 64), "cover-died-inside");
    expect(pathCoversHorizon(idleLive, 64), "cover-lived");
    expect(!pathCoversHorizon(idleLive, 128), "cover-short-live");
    {
        PathStats edge = pressDead;
        edge.deathTick = 63;
        expect(pathCoversHorizon(edge, 64), "cover-death-near-end");
        edge.deathTick = 64;
        expect(!pathCoversHorizon(edge, 64), "cover-death-at-horizon");
    }
    // Reuse vs fresh: same PathStats → identical decide (surviveTicks honesty).
    {
        Input reused;
        reused.press = pressDead;
        reused.idle = idleLive;
        reused.horizon = 64;
        Input fresh = reused;
        auto const a = decide(reused);
        auto const b = decide(fresh);
        expect(a.needDelaySearch == b.needDelaySearch, "reuse-eq-need");
        expect(a.idleSafer == b.idleSafer, "reuse-eq-idle");
        expect(a.bothDead == b.bothDead, "reuse-eq-both");
        expect(a.horizonUsed == b.horizonUsed, "reuse-eq-h");
    }

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
        expect(r.idleSurvived && !r.pressBetter && !r.bothDead, "decide-idle-flags");
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
        expect(!r.idleSafer && !r.bothDead, "decide-press-not-idle");
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
        expect(!r.bothDead, "decide-ext-not-both-dead");
    }

    // Extended still dies, idle lives → needDelaySearch (real coast escape).
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
        expect(!r.bothDead, "decide-ext-escape-not-both");
    }

    // Both die (idle later deathTick) → no Delay search / no LF idleSafer.
    {
        Input in;
        in.press = pressDead;
        in.idle = idleLater;
        in.horizon = 64;
        auto const r = decide(in);
        expect(r.bothDead, "decide-both-dead");
        expect(!r.needDelaySearch && !r.idleSafer, "decide-both-dead-no-search");
        expect(idleLooksSafer(pressDead, idleLater), "helper-idle-later-still");
    }

    // Both die, idle worse → bothDead, pressBetter, no search.
    {
        Input in;
        in.press = pressDead;
        in.idle = idleWorse;
        in.horizon = 64;
        auto const r = decide(in);
        expect(r.bothDead && r.pressBetter, "decide-both-dead-press-better");
        expect(!r.needDelaySearch && !r.idleSafer, "decide-both-dead-worse-no-search");
    }

    if (fails == 0) {
        report = "LongForesight selfCheck OK";
        return true;
    }
    report = fmt::format("LongForesight selfCheck {} fails: {}", fails, report);
    return false;
}
