#include "RoutePathBuilder.hpp"
#include "ModeAssist.hpp"

#include <algorithm>

int RoutePathBuilder::defaultStride(GameModeKind kind) {
    return kind == GameModeKind::Wave || kind == GameModeKind::Ship || kind == GameModeKind::Swing ? 2 : 4;
}

float RoutePathBuilder::corridorHalf(GameModeKind kind) { return ModeAssist::forKind(kind).routeCorridorHalfY(); }

GameModeKind RoutePathBuilder::modeFromTrace(int value) {
    if (value < 0 || value >= StageConfig::MODE_COUNT) return GameModeKind::Cube;
    return static_cast<GameModeKind>(value);
}

RoutePath RoutePathBuilder::buildFromTraces(std::vector<ShadowSimulator::BakeFrameTrace> const& frames, bool parityOk) {
    RoutePath out; out.parityOk = parityOk;
    if (!parityOk || frames.empty()) return out;
    int stride = 4;
    for (auto const& f : frames) stride = std::min(stride, defaultStride(modeFromTrace(f.mode)));
    while ((frames.size() / static_cast<size_t>(stride) + 2) > RoutePath::kMaxSamples) stride *= 2;
    out.sampleStrideTicks = stride;
    bool gap = false;
    for (size_t i = 0; i < frames.size(); ++i) {
        auto const& f = frames[i];
        if (f.dead || f.unknown) {
            gap = true;
            continue;
        }
        if (i % static_cast<size_t>(stride) != 0 && i + 1 != frames.size()) continue;
        RouteSample s;
        s.x = f.x; s.y = f.y; s.vy = f.yVel; s.mode = modeFromTrace(f.mode); s.tick = f.frame;
        s.grav = f.upsideDown ? -1 : 1; s.mini = f.mini; s.dual = f.dual;
        float const half = corridorHalf(s.mode); s.yLo = s.y - half; s.yHi = s.y + half;
        s.gapBefore = gap && !out.samples.empty();
        gap = false;
        if (!out.samples.empty() && !s.gapBefore) {
            auto const& prior = out.samples.back();
            int const dt = std::max(1, s.tick - prior.tick);
            s.vx = (s.x - prior.x) / static_cast<float>(dt);
        }
        out.samples.push_back(s);
    }
    if (out.samples.size() < 2) { out.samples.clear(); return out; }
    int start = 0;
    for (int i = 1; i <= static_cast<int>(out.samples.size()); ++i) {
        if (i == static_cast<int>(out.samples.size()) || out.samples[i].gapBefore ||
            out.samples[i].mode != out.samples[start].mode) {
            out.segments.push_back({start, i - 1, out.samples[start].mode, {}}); start = i;
        }
    }
    return out;
}

bool RoutePathBuilder::selfCheck(std::string& report) {
    ShadowSimulator::BakeFrameTrace a; a.frame = 0; a.x = 1; a.y = 2;
    ShadowSimulator::BakeFrameTrace b; b.frame = 4; b.x = 5; b.y = 6;
    auto p = buildFromTraces({a, b});
    bool const ok = p.sampleCount() == 2 && p.samples[0].yLo < p.samples[0].y && !p.segments.empty();
    report = ok ? "RoutePathBuilder selfCheck OK" : "RoutePathBuilder failed";
    return ok;
}
