#include "AssistHorizon.hpp"
#include "ModeAssist.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

int AssistHorizon::applyMul(int base, float capMul) {
    if (capMul < 0.25f) capMul = 0.25f;
    if (capMul > 3.f) capMul = 3.f;
    return std::max(8, static_cast<int>(std::lround(static_cast<float>(base) * capMul)));
}

AssistHorizon::Caps AssistHorizon::shortCaps(GameModeKind mode, int limit) {
    Caps c;
    ModeAssist::forKind(mode).fillShortCaps(limit, c.planCap, c.scoreHorizon, c.trajDraw);
    return c;
}

AssistHorizon::Caps AssistHorizon::longCaps(GameModeKind mode, int limit) {
    Caps c;
    ModeAssist::forKind(mode).fillLongCaps(limit, c.planCap, c.scoreHorizon, c.trajDraw);
    return c;
}

AssistHorizon::Caps AssistHorizon::resolve(
    GameModeKind mode,
    bool trustOk,
    int limit,
    float capMul,
    int settingsCap,
    int lookAheadSetting
) {
    Caps c = trustOk ? longCaps(mode, limit) : shortCaps(mode, limit);
    if (settingsCap > 0) {
        c.planCap = settingsCap;
        // Keep score/draw at least plan when user overrides cap.
        c.scoreHorizon = std::max(c.scoreHorizon, settingsCap);
        if (trustOk) c.trajDraw = c.scoreHorizon;
        else c.trajDraw = std::min(c.trajDraw, std::max(32, settingsCap + 16));
    }

    c.planCap = applyMul(c.planCap, capMul);
    c.scoreHorizon = applyMul(c.scoreHorizon, capMul);
    c.trajDraw = applyMul(c.trajDraw, capMul);

    if (lookAheadSetting > 0) {
        c.planCap = std::min(c.planCap, lookAheadSetting);
        c.scoreHorizon = std::min(c.scoreHorizon, lookAheadSetting);
        c.trajDraw = std::min(c.trajDraw, lookAheadSetting);
    }

    c.planCap = std::max(8, c.planCap);
    c.scoreHorizon = std::max(c.planCap, c.scoreHorizon);
    c.trajDraw = std::max(16, c.trajDraw);
    return c;
}

int AssistHorizon::minSurviveTicksForSuccess(int scoreHorizon, bool trustOk) {
    if (!trustOk) return 0; // short mode: threat-clear honesty alone
    int const h = std::max(8, scoreHorizon);
    // Floor 48 when the horizon is long enough; never demand more ticks than we score.
    // look-ahead-cap=14 used to force minSurvive=48 while planCap≤14 → Alive Delay impossible.
    return std::min(h, std::max(48, h / 2));
}

bool AssistHorizon::longPathSuccessOk(
    int surviveTicks,
    bool died,
    bool survivalSafeForDelay,
    int minSurvive
) {
    if (minSurvive <= 0) return true;
    if (died) {
        // Landing / recoverable / late-crash already imply continue-then-later.
        return survivalSafeForDelay;
    }
    return surviveTicks >= minSurvive;
}

bool AssistHorizon::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    auto const shortW = resolve(GameModeKind::Wave, false, 8, 1.f);
    expect(shortW.planCap == std::max(32, 8 + 16), "short-wave-plan");
    expect(shortW.trajDraw <= 56, "short-wave-draw-cap");

    auto const longW = resolve(GameModeKind::Wave, true, 8, 1.f);
    expect(longW.planCap >= 96, "long-wave-plan");
    expect(longW.scoreHorizon >= 160, "long-wave-score");
    expect(longW.trajDraw == longW.scoreHorizon, "draw-eq-trust-cap");

    auto const longShip = resolve(GameModeKind::Ship, true, 4, 1.f);
    expect(longShip.planCap >= 120, "long-ship-plan");
    expect(longShip.scoreHorizon >= 200, "long-ship-score");

    auto const shortCube = resolve(GameModeKind::Cube, false, 4, 1.f);
    expect(shortCube.planCap == 150, "short-cube");

    // settingsCap overrides base; lookAhead clamps.
    auto const clamped = resolve(GameModeKind::Wave, true, 8, 1.f, 0, 80);
    expect(clamped.planCap <= 80, "lookahead-clamp-plan");
    expect(clamped.trajDraw <= 80, "lookahead-clamp-draw");

    expect(minSurviveTicksForSuccess(160, false) == 0, "min-short-zero");
    expect(minSurviveTicksForSuccess(160, true) == 80, "min-long-half");
    expect(minSurviveTicksForSuccess(64, true) == 48, "min-long-floor48");
    expect(minSurviveTicksForSuccess(14, true) == 14, "min-long-cap-horizon");
    expect(minSurviveTicksForSuccess(80, true) == 48, "min-long-floor-vs-half");

    expect(longPathSuccessOk(100, false, false, 80), "alive-long-ok");
    expect(!longPathSuccessOk(40, false, false, 80), "alive-short-fail");
    expect(longPathSuccessOk(20, true, true, 80), "land-safe-ok");
    expect(!longPathSuccessOk(20, true, false, 80), "lethal-fail");
    expect(longPathSuccessOk(10, false, false, 0), "short-mode-pass");

    // Trust-long draw must beat short draw for wave.
    expect(longW.trajDraw > shortW.trajDraw, "long-draw-gt-short");

    if (fails == 0) {
        report = "AssistHorizon selfCheck OK";
        return true;
    }
    report = fmt::format("AssistHorizon selfCheck {} fails: {}", fails, report);
    return false;
}
