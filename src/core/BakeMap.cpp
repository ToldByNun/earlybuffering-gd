#include "BakeMap.hpp"
#include "ShadowSimulator.hpp"
#include "LevelProfile.hpp"
#include "../debug/AssistLog.hpp"

#include <Geode/utils/file.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace frire {

namespace {

std::filesystem::path bakesDir() {
    return Mod::get()->getSaveDir() / "bakes";
}

std::filesystem::path bakePath(std::string const& key) {
    return bakesDir() / (key + ".json");
}

int nextJumpFrame(std::vector<ReplayEvent> const& ev, int fromIndex, bool player2) {
    for (int i = fromIndex + 1; i < static_cast<int>(ev.size()); ++i) {
        if (ev[i].button != 1) continue;
        if (ev[i].player2 != player2) continue;
        if (ev[i].down) return ev[i].frame;
    }
    return -1;
}

}

BakeMap& BakeMap::get() {
    static BakeMap inst;
    return inst;
}

void BakeMap::clear() {
    m_data = {};
    m_loaded = false;
}

void BakeMap::clearPendingReplay() {
    m_replay = {};
    m_hasReplay = false;
}

void BakeMap::setPendingReplay(ImportedReplay replay) {
    m_replay = std::move(replay);
    m_hasReplay = true;
    AssistLog::info(fmt::format(
        "BAKE: loaded replay {} ({} events, {:.0f} tps, {})",
        m_replay.sourceName, m_replay.events.size(), m_replay.tps, m_replay.format
    ));
}

void BakeMap::loadForLevel(GJGameLevel* level) {
    m_data = {};
    m_loaded = false;
    if (!level) return;
    m_data.levelKey = LevelProfile::makeKey(level);
    m_loaded = true;
    auto res = readFile();
    if (res.isOk()) {
        AssistLog::debug(fmt::format(
            "BAKE: loaded map {} ({} clicks, {})",
            m_data.levelKey, m_data.clicks.size(), m_data.sourceReplay
        ));
    }
}

void BakeMap::save() const {
    if (!m_loaded || m_data.levelKey.empty()) return;
    auto res = writeFile();
    if (res.isOk()) {
        AssistLog::debug(fmt::format(
            "BAKE: saved {} ({} clicks)", m_data.levelKey, m_data.clicks.size()
        ));
    }
    else {
        AssistLog::debug(fmt::format("BAKE: save failed: {}", res.unwrapErr()));
    }
}

std::string BakeMap::statusLine() const {
    if (m_hasReplay && !m_data.clicks.empty()) {
        return fmt::format("replay {} | bake {} clicks", m_replay.sourceName, m_data.clicks.size());
    }
    if (m_hasReplay) return fmt::format("replay {} | no bake", m_replay.sourceName);
    if (!m_data.clicks.empty()) return fmt::format("bake {} clicks ({})", m_data.clicks.size(), m_data.sourceReplay);
    return "no bake";
}

BakedClick const* BakeMap::findNear(
    float playerX, int approxFrame, bool down, bool player2
) const {
    BakedClick const* best = nullptr;
    float bestDx = LOOKUP_X_TOL + 1.f;
    for (auto const& c : m_data.clicks) {
        if (c.down != down || c.player2 != player2) continue;
        float dx = std::fabs(c.x - playerX);
        if (dx > LOOKUP_X_TOL) continue;
        // Prefer clicks not far behind the estimated frame
        if (approxFrame >= 0 && c.nominalFrame + 240 < approxFrame) continue;
        if (dx < bestDx) {
            bestDx = dx;
            best = &c;
        }
    }
    return best;
}

bool BakeMap::testShiftedClick(
    PlayLayer* layer,
    int startSnapshotSlot,
    std::vector<ReplayEvent> timeline,
    int eventIndex,
    int testFrame,
    int untilFrame,
    float dt
) const {
    if (eventIndex < 0 || eventIndex >= static_cast<int>(timeline.size())) return false;
    timeline[static_cast<size_t>(eventIndex)].frame = testFrame;
    std::stable_sort(timeline.begin(), timeline.end(), [](auto const& a, auto const& b) {
        return a.frame < b.frame;
    });
    auto r = ShadowSimulator::get().runBakeTimeline(
        layer, startSnapshotSlot, timeline, dt, untilFrame, -1
    );
    return !r.died;
}

Result<int> BakeMap::bake(PlayLayer* layer, int startSnapshotSlot, int bakeRadius) {
    if (!layer) return Err("No PlayLayer");
    if (!m_hasReplay || m_replay.events.empty()) return Err("No replay loaded");
    if (startSnapshotSlot < 0) return Err("No start snapshot (reset once, then bake)");

    double tps = m_replay.tps > 0.0 ? m_replay.tps : 240.0;
    float dt = static_cast<float>(1.0 / tps);
    if (bakeRadius <= 0) {
        bakeRadius = std::max(8, StageConfig::ticksAll());
    }

    BakeMapData out;
    out.levelKey = m_data.levelKey.empty() && layer->m_level
        ? LevelProfile::makeKey(layer->m_level)
        : m_data.levelKey;
    if (out.levelKey.empty()) out.levelKey = "unknown";
    out.sourceReplay = m_replay.sourceName;
    out.tps = tps;

    auto const& ev = m_replay.events;
    bool const doRelease = StageConfig::assistRelease();

    for (int i = 0; i < static_cast<int>(ev.size()); ++i) {
        auto const& e = ev[static_cast<size_t>(i)];
        if (e.button != 1) continue; // jump only
        if (!e.down && !doRelease) continue;

        int const F = e.frame;
        int next = nextJumpFrame(ev, i, e.player2);
        int horizon = ShadowSimulator::modelFor(layer->m_player1, StageConfig::ticksAll()).cap;
        int until = next > 0 ? std::min(next, F + horizon) : F + horizon;
        until = std::max(until, F + 1);

        // Sample X / mode at nominal frame
        auto sample = ShadowSimulator::get().runBakeTimeline(
            layer, startSnapshotSlot, ev, dt, F, F
        );
        float x = sample.sampled ? sample.xAtSample : 0.f;
        auto mode = sample.sampled ? sample.modeAtSample : GameModeKind::Cube;

        // Nominal must itself survive (otherwise skip)
        if (!testShiftedClick(layer, startSnapshotSlot, ev, i, F, until, dt)) {
            AssistLog::debug(fmt::format("BAKE: skip click @{} (nominal dies)", F));
            continue;
        }

        int earliest = F;
        int lo = std::max(0, F - bakeRadius);
        int hi = F;
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            if (testShiftedClick(layer, startSnapshotSlot, ev, i, mid, until, dt)) {
                hi = mid;
            }
            else {
                lo = mid + 1;
            }
        }
        earliest = lo;

        int latest = F;
        lo = F;
        hi = F + bakeRadius;
        while (lo < hi) {
            int mid = lo + (hi - lo + 1) / 2;
            if (testShiftedClick(layer, startSnapshotSlot, ev, i, mid, until, dt)) {
                lo = mid;
            }
            else {
                hi = mid - 1;
            }
        }
        latest = lo;

        BakedClick c;
        c.nominalFrame = F;
        c.earliest = earliest;
        c.latest = latest;
        c.down = e.down;
        c.player2 = e.player2;
        c.x = x;
        c.mode = mode;
        out.clicks.push_back(c);

        AssistLog::debug(fmt::format(
            "BAKE: {} @{} window [{},{}] x={:.1f}",
            e.down ? "press" : "release", F, earliest, latest, x
        ));
    }

    m_data = std::move(out);
    m_loaded = true;
    save();
    AssistLog::info(fmt::format(
        "BAKE: {} clicks from {} @ {:.0f} tps",
        m_data.clicks.size(), m_data.sourceReplay, m_data.tps
    ));
    return Ok(static_cast<int>(m_data.clicks.size()));
}

Result<> BakeMap::writeFile() const {
    (void)file::createDirectoryAll(bakesDir());
    matjson::Value root = matjson::Value::object();
    root["version"] = 1;
    root["key"] = m_data.levelKey;
    root["source"] = m_data.sourceReplay;
    root["tps"] = m_data.tps;

    std::vector<matjson::Value> arr;
    arr.reserve(m_data.clicks.size());
    for (auto const& c : m_data.clicks) {
        matjson::Value j = matjson::Value::object();
        j["nominal"] = c.nominalFrame;
        j["earliest"] = c.earliest;
        j["latest"] = c.latest;
        j["down"] = c.down;
        j["player2"] = c.player2;
        j["x"] = c.x;
        j["mode"] = StageConfig::kindName(c.mode);
        arr.push_back(std::move(j));
    }
    root["clicks"] = arr;
    return file::writeToJson(bakePath(m_data.levelKey), root);
}

Result<> BakeMap::readFile() {
    auto res = file::readJson(bakePath(m_data.levelKey));
    if (res.isErr()) return Err(res.unwrapErr());
    auto root = res.unwrap();

    if (root.contains("source")) m_data.sourceReplay = root["source"].asString().unwrapOr("");
    if (root.contains("tps")) m_data.tps = root["tps"].asDouble().unwrapOr(240.0);
    m_data.clicks.clear();
    if (!root.contains("clicks") || !root["clicks"].isArray()) return Ok();

    for (auto const& j : root["clicks"]) {
        BakedClick c;
        c.nominalFrame = static_cast<int>(j["nominal"].asInt().unwrapOr(0));
        c.earliest = static_cast<int>(j["earliest"].asInt().unwrapOr(c.nominalFrame));
        c.latest = static_cast<int>(j["latest"].asInt().unwrapOr(c.nominalFrame));
        c.down = j["down"].asBool().unwrapOr(true);
        c.player2 = j["player2"].asBool().unwrapOr(false);
        c.x = static_cast<float>(j["x"].asDouble().unwrapOr(0.0));
        auto modeName = j.contains("mode") ? j["mode"].asString().unwrapOr("cube") : "cube";
        c.mode = GameModeKind::Cube;
        for (int i = 0; i < StageConfig::MODE_COUNT; ++i) {
            if (modeName == StageConfig::kindName(static_cast<GameModeKind>(i))) {
                c.mode = static_cast<GameModeKind>(i);
                break;
            }
        }
        m_data.clicks.push_back(c);
    }
    return Ok();
}

}
