#ifndef EARLYBUFFER_CORE_ROUTEPATH_HPP
#define EARLYBUFFER_CORE_ROUTEPATH_HPP

#include "../config/StageConfig.hpp"

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

// One pose sample along a validated replay ghost path.
struct RouteSample {
    float x = 0.f;
    float y = 0.f;
    GameModeKind mode = GameModeKind::Cube;
    float vx = 0.f;
    float vy = 0.f;
    int grav = 1; // +1 / -1
    bool mini = false;
    bool dual = false;
    bool p2 = false;
    int tick = 0;
    float yLo = 0.f;
    float yHi = 0.f;
    bool gapBefore = false;
};

struct RouteSegment {
    int i0 = 0;
    int i1 = 0; // inclusive sample indices
    GameModeKind mode = GameModeKind::Cube;
    std::vector<std::string> flags; // e.g. "portal"
};

// Validated replay corridor (P1). Lookup by X; ModeAssist supplies half-width.
struct RoutePath {
    static constexpr int kSchema = 1;
    static constexpr int kMaxSamples = 4096;
    static constexpr char kReplayPattern[] = "replay-pattern";

    int schema = kSchema;
    std::string source = "replay-validated";
    bool parityOk = true;
    int sampleStrideTicks = 4;
    std::vector<RouteSegment> segments;
    std::vector<RouteSample> samples;

    bool empty() const { return samples.empty(); }
    bool isReplayPattern() const { return source == kReplayPattern; }
    bool usable() const { return !empty() && (parityOk || source == kReplayPattern); }
    int sampleCount() const { return static_cast<int>(samples.size()); }

    // Binary search by x; interpolate y/yLo/yHi. false if empty / x outside span.
    bool lookupX(float x, RouteSample& out) const;

    // Segment containing sample index; nullptr if none.
    RouteSegment const* segmentAt(int sampleIndex) const;

    matjson::Value toJson() const;
    static geode::Result<RoutePath> fromJson(matjson::Value const& root);

    static bool selfCheck(std::string& report);
};

#endif // EARLYBUFFER_CORE_ROUTEPATH_HPP
