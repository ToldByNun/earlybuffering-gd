#include "ModeAssist.hpp"
#include "AssistHorizon.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace {

CubeModeAssist g_cube;
BallModeAssist g_ball;
RobotModeAssist g_robot;
ShipModeAssist g_ship;
UfoModeAssist g_ufo;
WaveModeAssist g_wave;
SpiderModeAssist g_spider;
SwingModeAssist g_swing;

// StageConfig.cpp defaultLatencyWeight table (parity mirror).
float expectedBaseLatency(GameModeKind kind) {
    switch (kind) {
    case GameModeKind::Ship: return 0.75f;
    case GameModeKind::Ball: return 0.35f;
    case GameModeKind::Ufo: return 0.35f;
    case GameModeKind::Wave: return 1.00f;
    case GameModeKind::Robot: return 0.40f;
    case GameModeKind::Spider: return 0.40f;
    case GameModeKind::Swing: return 0.85f;
    case GameModeKind::Cube:
    default: return 0.25f;
    }
}

} // namespace

ModeAssist const& ModeAssist::forKind(GameModeKind kind) {
    switch (kind) {
    case GameModeKind::Ball: return g_ball;
    case GameModeKind::Ship: return g_ship;
    case GameModeKind::Ufo: return g_ufo;
    case GameModeKind::Wave: return g_wave;
    case GameModeKind::Robot: return g_robot;
    case GameModeKind::Spider: return g_spider;
    case GameModeKind::Swing: return g_swing;
    case GameModeKind::Cube:
    default: return g_cube;
    }
}

float ModeAssist::latencyWeight(bool spamDense) const {
    float const base = baseLatencyWeight();
    if (!spamDense) {
        return base;
    }
    // AssistController::fillPlanRobustLatency Wave spam soften (parity).
    if (kind() == GameModeKind::Wave && base > 0.5f) {
        return base * 0.65f;
    }
    return base;
}

void ModeAssist::fillSnapshot(ModeAssistSnapshot& out, int windowTicks, bool husAllowed) const {
    out = ModeAssistSnapshot{};
    out.kind = kind();
    out.windowTicks = std::max(1, windowTicks);
    out.latencyWeight = baseLatencyWeight();
    out.latencyWeightSpam = latencyWeight(true);
    out.spamWindowTicks = spamWindowTicks();
    out.spamMinCount = spamMinCount();
    out.spamMaxMeanGap = spamMaxMeanGap();
    out.continuous = continuous();
    out.releaseAssistAlways = releaseAssistAlways();
    out.tapRelease = tapRelease();
    out.featheringIsControl = featheringIsControl();
    out.waveProximityPassAllowed = waveProximityPassAllowed();
    out.waveFutilePassAllowed = waveFutilePassAllowed();
    out.prefixWaveTurn = prefixWaveTurn();
    out.prefixShipRecover = prefixShipRecover();
    out.prefixHoldBranch = prefixHoldBranch();
    out.robustKFloor = robustKFloor();
    out.robustKFloorSpam = robustKFloorSpam();
    out.routeCorridorHalfY = routeCorridorHalfY();
    out.foresightYReach = foresightYReach();
    out.hazardYReach = hazardYReach();
    out.allowHus = husAllowed;
}

ModeAssistSnapshot ModeAssist::snapshotFor(
    GameModeKind kind,
    int windowTicks,
    bool husAllowed
) {
    ModeAssistSnapshot snap;
    forKind(kind).fillSnapshot(snap, windowTicks, husAllowed);
    return snap;
}

// --- DiscreteJump (cube/ball): AssistHorizon short/long SoT ---

void DiscreteJumpAssist::fillShortCaps(
    int /*limit*/,
    int& planCap,
    int& scoreHorizon,
    int& trajDraw
) const {
    planCap = 150;
    scoreHorizon = 150;
    trajDraw = 96;
}

void DiscreteJumpAssist::fillLongCaps(
    int limit,
    int& planCap,
    int& scoreHorizon,
    int& trajDraw
) const {
    int const x = limit > 0 ? limit : 1;
    planCap = std::max(180, x + 80);
    scoreHorizon = std::max(240, planCap);
    trajDraw = scoreHorizon;
}

// --- Feather (ship/swing) ---

void FeatherContinuousAssist::fillShortCaps(
    int limit,
    int& planCap,
    int& scoreHorizon,
    int& trajDraw
) const {
    int const x = limit > 0 ? limit : 1;
    planCap = std::max(64, x * 3);
    scoreHorizon = planCap;
    trajDraw = std::clamp(planCap + 16, 40, 72);
}

void FeatherContinuousAssist::fillLongCaps(
    int limit,
    int& planCap,
    int& scoreHorizon,
    int& trajDraw
) const {
    int const x = limit > 0 ? limit : 1;
    planCap = std::max(120, x * 4);
    scoreHorizon = std::max(200, planCap);
    trajDraw = scoreHorizon;
}

// --- Tap continuous (ufo/spider) ---
// AssistHorizon short: Ufo max(40,x+20) vs Spider max(36,x+16); long shared.

void TapContinuousAssist::fillShortCaps(
    int limit,
    int& planCap,
    int& scoreHorizon,
    int& trajDraw
) const {
    int const x = limit > 0 ? limit : 1;
    if (kind() == GameModeKind::Spider) {
        planCap = std::max(36, x + 16);
    }
    else {
        planCap = std::max(40, x + 20);
    }
    scoreHorizon = planCap;
    trajDraw = std::clamp(planCap + 20, 36, 64);
}

void TapContinuousAssist::fillLongCaps(
    int limit,
    int& planCap,
    int& scoreHorizon,
    int& trajDraw
) const {
    int const x = limit > 0 ? limit : 1;
    planCap = std::max(96, x + 48);
    scoreHorizon = std::max(160, planCap);
    trajDraw = scoreHorizon;
}

// --- Robot (discrete + releaseAlways; short=cube, long score floor 280) ---

void RobotModeAssist::fillShortCaps(
    int /*limit*/,
    int& planCap,
    int& scoreHorizon,
    int& trajDraw
) const {
    planCap = 150;
    scoreHorizon = 150;
    trajDraw = 96;
}

void RobotModeAssist::fillLongCaps(
    int limit,
    int& planCap,
    int& scoreHorizon,
    int& trajDraw
) const {
    int const x = limit > 0 ? limit : 1;
    planCap = std::max(180, x + 80);
    scoreHorizon = std::max(280, planCap);
    trajDraw = scoreHorizon;
}

// --- Wave ---

void WaveModeAssist::fillShortCaps(
    int limit,
    int& planCap,
    int& scoreHorizon,
    int& trajDraw
) const {
    int const x = limit > 0 ? limit : 1;
    planCap = std::max(32, x + 16);
    scoreHorizon = planCap;
    trajDraw = std::clamp(planCap + 24, 32, 56);
}

void WaveModeAssist::fillLongCaps(
    int limit,
    int& planCap,
    int& scoreHorizon,
    int& trajDraw
) const {
    int const x = limit > 0 ? limit : 1;
    planCap = std::max(96, x + 64);
    scoreHorizon = std::max(160, planCap);
    trajDraw = scoreHorizon;
}

bool ModeAssist::selfCheckKind(GameModeKind kind, std::string& report) {
    ModeAssist const& m = forKind(kind);
    ModeAssistSnapshot snap = snapshotFor(kind, 12, true);

    auto fail = [&](char const* why) {
        std::ostringstream oss;
        oss << "ModeAssist." << m.name() << ": " << why;
        report = oss.str();
        return false;
    };

    if (snap.kind != kind) {
        return fail("snapshot kind mismatch");
    }
    if (std::fabs(snap.latencyWeight - m.baseLatencyWeight()) > 1e-6f) {
        return fail("latencyWeight != base");
    }
    if (std::fabs(m.baseLatencyWeight() - expectedBaseLatency(kind)) > 1e-6f) {
        return fail("λ vs StageConfig default");
    }
    if (kind == GameModeKind::Wave) {
        float const expect = m.baseLatencyWeight() * 0.65f;
        if (std::fabs(snap.latencyWeightSpam - expect) > 1e-5f) {
            return fail("wave spam λ soften");
        }
        if (!snap.waveProximityPassAllowed || !snap.waveFutilePassAllowed) {
            return fail("wave pass flags");
        }
        if (!snap.prefixWaveTurn || !snap.continuous || !snap.releaseAssistAlways) {
            return fail("wave continuous/prefix");
        }
        if (snap.spamWindowTicks != 10 || snap.spamMinCount != 3) {
            return fail("wave spam table");
        }
    }
    if (kind == GameModeKind::Ship || kind == GameModeKind::Swing) {
        if (!snap.featheringIsControl || !snap.continuous || !snap.releaseAssistAlways) {
            return fail("feather continuous");
        }
        if (snap.spamWindowTicks != 12 || snap.spamMinCount != 3) {
            return fail("feather spam table");
        }
        if (!snap.prefixShipRecover) {
            return fail("ship recover prefix");
        }
    }
    if (kind == GameModeKind::Ufo || kind == GameModeKind::Spider) {
        if (!snap.continuous || !snap.tapRelease || snap.releaseAssistAlways) {
            return fail("tap-continuous flags");
        }
    }
    if (kind == GameModeKind::Robot) {
        if (snap.continuous || !snap.releaseAssistAlways || snap.tapRelease) {
            return fail("robot releaseAlways + discrete");
        }
        if (!snap.prefixHoldBranch) {
            return fail("robot hold branch");
        }
    }
    if (kind == GameModeKind::Cube || kind == GameModeKind::Ball) {
        if (snap.continuous || snap.releaseAssistAlways || !snap.tapRelease) {
            return fail("discrete jump flags");
        }
    }

    // Horizon short/long must match AssistHorizon SoT (limit=8 like its selfCheck).
    int plan = 0, score = 0, traj = 0;
    m.fillShortCaps(8, plan, score, traj);
    switch (kind) {
    case GameModeKind::Cube:
    case GameModeKind::Ball:
    case GameModeKind::Robot:
        if (plan != 150 || score != 150 || traj != 96) {
            return fail("short horizon discrete");
        }
        break;
    case GameModeKind::Ship:
    case GameModeKind::Swing:
        if (plan != 64 || score != 64 || traj != 72) {
            // max(64, 8*3)=64; clamp(64+16,40,72)=72
            return fail("short horizon feather");
        }
        break;
    case GameModeKind::Ufo:
        if (plan != 40 || score != 40 || traj != 60) {
            // max(40,8+20)=40; clamp(40+20,36,64)=60
            return fail("short horizon ufo");
        }
        break;
    case GameModeKind::Spider:
        if (plan != 36 || score != 36 || traj != 56) {
            // max(36,8+16)=36; clamp(36+20,36,64)=56
            return fail("short horizon spider");
        }
        break;
    case GameModeKind::Wave:
        if (plan != 32 || score != 32 || traj != 56) {
            // max(32,8+16)=32; clamp(32+24,32,56)=56
            return fail("short horizon wave");
        }
        break;
    default:
        break;
    }

    m.fillLongCaps(8, plan, score, traj);
    switch (kind) {
    case GameModeKind::Wave:
        if (plan < 96 || score < 160) {
            return fail("long horizon wave");
        }
        break;
    case GameModeKind::Ship:
    case GameModeKind::Swing:
        if (plan < 120 || score < 200) {
            return fail("long horizon feather");
        }
        break;
    case GameModeKind::Cube:
    case GameModeKind::Ball:
        if (plan < 180 || score < 240) {
            return fail("long horizon discrete");
        }
        break;
    case GameModeKind::Robot:
        if (plan < 180 || score < 280) {
            return fail("long horizon robot");
        }
        break;
    default:
        break;
    }

    report.clear();
    return true;
}

bool ModeAssist::selfCheck(std::string& report) {
    static GameModeKind const kKinds[] = {
        GameModeKind::Cube,
        GameModeKind::Ship,
        GameModeKind::Ball,
        GameModeKind::Ufo,
        GameModeKind::Wave,
        GameModeKind::Robot,
        GameModeKind::Spider,
        GameModeKind::Swing,
    };
    for (GameModeKind k : kKinds) {
        if (!selfCheckKind(k, report)) {
            return false;
        }
    }
    // Cross-check AssistHorizon::resolve (mul=1, no settings/lookahead clamp).
    for (GameModeKind k : kKinds) {
        ModeAssist const& m = forKind(k);
        int mp = 0, ms = 0, mt = 0;
        m.fillShortCaps(8, mp, ms, mt);
        auto const shortH = AssistHorizon::resolve(k, false, 8, 1.f);
        // applyMul(base, 1) → max(8, base); floors in resolve push traj ≥16.
        if (shortH.planCap != std::max(8, mp)
            || shortH.scoreHorizon != std::max(shortH.planCap, std::max(8, ms))
            || shortH.trajDraw != std::max(16, std::max(8, mt))) {
            std::ostringstream oss;
            oss << "ModeAssist." << m.name() << ": short vs AssistHorizon drift";
            report = oss.str();
            return false;
        }
        m.fillLongCaps(8, mp, ms, mt);
        auto const longH = AssistHorizon::resolve(k, true, 8, 1.f);
        if (longH.planCap != std::max(8, mp)
            || longH.scoreHorizon != std::max(longH.planCap, std::max(8, ms))
            || longH.trajDraw != std::max(16, std::max(8, mt))) {
            std::ostringstream oss;
            oss << "ModeAssist." << m.name() << ": long vs AssistHorizon drift";
            report = oss.str();
            return false;
        }
    }
    report.clear();
    return true;
}
