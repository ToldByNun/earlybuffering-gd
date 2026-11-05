#include "RoutePath.hpp"

#include <algorithm>
#include <cmath>

namespace {

GameModeKind parseMode(matjson::Value const& value) {
    auto const name = value.asString().unwrapOr("cube");
    for (int i = 0; i < StageConfig::MODE_COUNT; ++i) {
        auto const kind = static_cast<GameModeKind>(i);
        if (name == StageConfig::kindName(kind)) return kind;
    }
    return GameModeKind::Cube;
}

} // namespace

bool RoutePath::lookupX(float x, RouteSample& out) const {
    if (samples.empty() || x < samples.front().x || x > samples.back().x) return false;
    auto const it = std::lower_bound(samples.begin(), samples.end(), x,
        [](RouteSample const& sample, float value) { return sample.x < value; });
    if (it == samples.begin() || it == samples.end()) {
        out = it == samples.end() ? samples.back() : *it;
        return true;
    }
    auto const& b = *it;
    auto const& a = *(it - 1);
    float const dx = b.x - a.x;
    float const t = std::fabs(dx) > 0.001f ? std::clamp((x - a.x) / dx, 0.f, 1.f) : 0.f;
    out = a;
    out.x = x;
    out.y = a.y + (b.y - a.y) * t;
    out.yLo = a.yLo + (b.yLo - a.yLo) * t;
    out.yHi = a.yHi + (b.yHi - a.yHi) * t;
    out.vx = a.vx + (b.vx - a.vx) * t;
    out.vy = a.vy + (b.vy - a.vy) * t;
    return true;
}

RouteSegment const* RoutePath::segmentAt(int sampleIndex) const {
    for (auto const& segment : segments) {
        if (sampleIndex >= segment.i0 && sampleIndex <= segment.i1) return &segment;
    }
    return nullptr;
}

matjson::Value RoutePath::toJson() const {
    matjson::Value root = matjson::Value::object();
    root["schema"] = schema;
    root["source"] = source;
    root["parityOk"] = parityOk;
    root["sampleStrideTicks"] = sampleStrideTicks;
    std::vector<matjson::Value> points;
    for (auto const& s : samples) {
        matjson::Value j = matjson::Value::object();
        j["x"] = s.x; j["y"] = s.y; j["yLo"] = s.yLo; j["yHi"] = s.yHi;
        j["vx"] = s.vx; j["vy"] = s.vy; j["mode"] = StageConfig::kindName(s.mode);
        j["grav"] = s.grav; j["mini"] = s.mini; j["dual"] = s.dual;
        j["p2"] = s.p2; j["tick"] = s.tick;
        points.push_back(std::move(j));
    }
    root["samples"] = points;
    std::vector<matjson::Value> ranges;
    for (auto const& s : segments) {
        matjson::Value j = matjson::Value::object();
        j["i0"] = s.i0; j["i1"] = s.i1; j["mode"] = StageConfig::kindName(s.mode);
        j["flags"] = s.flags;
        ranges.push_back(std::move(j));
    }
    root["segments"] = ranges;
    return root;
}

geode::Result<RoutePath> RoutePath::fromJson(matjson::Value const& root) {
    if (root["schema"].asInt().unwrapOr(0) != kSchema) return geode::Err("unsupported route schema");
    if (!root.contains("samples") || !root["samples"].isArray()) return geode::Err("route samples missing");
    RoutePath out;
    out.schema = kSchema;
    out.source = root["source"].asString().unwrapOr("replay-validated");
    out.parityOk = root["parityOk"].asBool().unwrapOr(false);
    out.sampleStrideTicks = static_cast<int>(root["sampleStrideTicks"].asInt().unwrapOr(4));
    for (auto const& j : root["samples"]) {
        RouteSample s;
        s.x = static_cast<float>(j["x"].asDouble().unwrapOr(0));
        s.y = static_cast<float>(j["y"].asDouble().unwrapOr(0));
        s.yLo = static_cast<float>(j["yLo"].asDouble().unwrapOr(s.y));
        s.yHi = static_cast<float>(j["yHi"].asDouble().unwrapOr(s.y));
        s.vx = static_cast<float>(j["vx"].asDouble().unwrapOr(0));
        s.vy = static_cast<float>(j["vy"].asDouble().unwrapOr(0));
        s.mode = parseMode(j["mode"]); s.grav = static_cast<int>(j["grav"].asInt().unwrapOr(1));
        s.mini = j["mini"].asBool().unwrapOr(false); s.dual = j["dual"].asBool().unwrapOr(false);
        s.p2 = j["p2"].asBool().unwrapOr(false); s.tick = static_cast<int>(j["tick"].asInt().unwrapOr(0));
        out.samples.push_back(s);
    }
    if (out.samples.empty() || out.samples.size() > kMaxSamples) return geode::Err("route sample count invalid");
    if (!std::is_sorted(out.samples.begin(), out.samples.end(), [](auto const& a, auto const& b) { return a.x < b.x; })) return geode::Err("route samples unsorted");
    if (root.contains("segments") && root["segments"].isArray()) for (auto const& j : root["segments"]) {
        RouteSegment s;
        s.i0 = static_cast<int>(j["i0"].asInt().unwrapOr(0)); s.i1 = static_cast<int>(j["i1"].asInt().unwrapOr(-1));
        s.mode = parseMode(j["mode"]);
        if (j.contains("flags") && j["flags"].isArray()) for (auto const& flag : j["flags"]) s.flags.push_back(flag.asString().unwrapOr(""));
        if (s.i0 < 0 || s.i1 < s.i0 || s.i1 >= static_cast<int>(out.samples.size())) return geode::Err("route segment invalid");
        out.segments.push_back(std::move(s));
    }
    return geode::Ok(std::move(out));
}

bool RoutePath::selfCheck(std::string& report) {
    RoutePath p; p.samples = {{0, 10}, {10, 30}};
    RouteSample got;
    auto decoded = fromJson(p.toJson());
    bool const ok = p.lookupX(5, got) && std::fabs(got.y - 20.f) < .01f
        && !p.lookupX(-1, got) && decoded.isOk()
        && decoded.unwrap().sampleCount() == 2;
    report = ok ? "RoutePath selfCheck OK" : "RoutePath lookup failed";
    return ok;
}
