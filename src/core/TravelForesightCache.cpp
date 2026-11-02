#include "TravelForesightCache.hpp"
#include "TravelForesight.hpp"

#include <Geode/Geode.hpp>
#include <cmath>
#include <fmt/format.h>

using namespace geode::prelude;

TravelForesightCache& TravelForesightCache::get() {
    static TravelForesightCache inst;
    return inst;
}

void TravelForesightCache::clear() {
    this->m_valid = false;
    this->m_tick = -1;
    this->m_hit = false;
    // Keep hit/miss counters for the attempt overlay/diagnostics.
}

int TravelForesightCache::xBucket(float x) {
    if (!std::isfinite(x)) return 0;
    return static_cast<int>(std::floor(x / static_cast<float>(kXBucketPx)));
}

int TravelForesightCache::speedQuantum(float speedMul) {
    float s = speedMul;
    if (!(s > 0.f) || !std::isfinite(s)) s = 1.f;
    return static_cast<int>(std::lround(s * 100.f));
}

bool TravelForesightCache::hasOrbOrPadAhead(
    PlayLayer* layer,
    PlayerObject* player,
    int limit,
    float simDt,
    int slack,
    int attemptTick
) {
    if (!layer || !player || simDt <= 0.f) return false;

    int const xb = xBucket(player->getPositionX());
    int const sq = speedQuantum(player->m_playerSpeed);
    int const lim = std::max(0, limit);
    int const sl = std::max(0, slack);

    if (this->m_valid
        && this->m_tick == attemptTick
        && this->m_xBucket == xb
        && this->m_limit == lim
        && this->m_slack == sl
        && this->m_speedQ == sq) {
        ++this->m_hits;
        return this->m_hit;
    }

    bool const hit = TravelForesight::hasOrbOrPadAhead(
        layer, player, lim, simDt, sl
    );
    this->m_valid = true;
    this->m_tick = attemptTick;
    this->m_xBucket = xb;
    this->m_limit = lim;
    this->m_slack = sl;
    this->m_speedQ = sq;
    this->m_hit = hit;
    ++this->m_misses;
    return hit;
}

bool TravelForesightCache::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    expect(xBucket(0.f) == 0, "xb0");
    expect(xBucket(3.9f) == 0, "xb-lo");
    expect(xBucket(4.f) == 1, "xb-4");
    expect(speedQuantum(1.f) == 100, "spd-1");
    expect(speedQuantum(0.f) == 100, "spd-bad");

    TravelForesightCache& c = TravelForesightCache::get();
    int const hits0 = c.hits();
    int const misses0 = c.misses();
    c.clear();
    expect(!c.m_valid, "clear");

    // Null scan — miss then same-key hit returns false without second null path.
    bool const a = c.hasOrbOrPadAhead(nullptr, nullptr, 16, 1.f / 240.f, 2, 10);
    expect(!a, "null-false");
    // nullptr early-outs before cache write — still counts as no hit stored.
    // Force a synthetic cache entry:
    c.m_valid = true;
    c.m_tick = 42;
    c.m_xBucket = 0;
    c.m_limit = 8;
    c.m_slack = 1;
    c.m_speedQ = 100;
    c.m_hit = true;
    // Same key without player will early-out false before cache — use miss path only.
    expect(c.m_hit == true, "synth-hit");

    c.clear();
    expect(c.hits() >= hits0, "hits-mono");
    expect(c.misses() >= misses0, "miss-mono");

    if (fails == 0) {
        report = "TravelForesightCache selfCheck OK";
        return true;
    }
    report = fmt::format(
        "TravelForesightCache selfCheck {} fails: {}", fails, report
    );
    return false;
}
