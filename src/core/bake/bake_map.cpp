#include "bake_map.hpp"
#include "../../config/stage_config.hpp"
#include "../../debug/assist_log.hpp"
#include "../assist/assist_clock.hpp"
#include "../learning/level_profile.hpp"
#include "../replay/replay_import.hpp"
#include "generation/bake_brute_generator.hpp"
#include "generation/bake_replay_generator.hpp"
#include "windows/bake_window_index.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

std::filesystem::path BakeMapImplementation::bakesDir() {
    return geode::prelude::Mod::get()->getSaveDir() / "bakes";
}

std::filesystem::path BakeMapImplementation::bakePath(std::string const& key) {
    return BakeMapImplementation::bakesDir() / (key + ".json");
}

int BakeMapImplementation::nextJumpFrame(std::vector<ReplayEvent> const& ev, int fromIndex,
                                         bool player2) {
    for (int index = fromIndex + 1; index < static_cast<int>(ev.size()); ++index) {
        if (ev[index].button != 1)
            continue;
        if (ev[index].player2 != player2)
            continue;
        if (ev[index].down)
            return ev[index].frame;
    }
    return -1;
}

int BakeMapImplementation::resolveRadius(int bakeRadius, BakeMethod method) {
    if (bakeRadius > 0)
        return bakeRadius;
    int base = StageConfig::ticksAll();
    if (method == BakeMethod::BruteForce) {
        return std::max(32, base * 4);
    }
    return std::max(8, base);
}

// After TPS remap, same-source-frame press+release can collapse to one bake
// frame. Push releases forward so every press is held ≥1 bake tick.
void BakeMapImplementation::ensureMinHoldTicks(std::vector<ReplayEvent>& ev, int minHold) {
    if (minHold <= 0 || ev.empty())
        return;
    for (size_t index = 0; index < ev.size(); ++index) {
        if (!ev[index].down || ev[index].button != 1)
            continue;
        for (size_t otherIndex = index + 1; otherIndex < ev.size(); ++otherIndex) {
            if (ev[otherIndex].button != ev[index].button)
                continue;
            if (ev[otherIndex].player2 != ev[index].player2)
                continue;
            if (ev[otherIndex].down)
                break; // next press
            if (ev[otherIndex].frame <= ev[index].frame) {
                ev[otherIndex].frame = ev[index].frame + minHold;
            }
            break;
        }
    }
    std::stable_sort(ev.begin(), ev.end(), [](auto const& a, auto const& b) {
        if (a.frame != b.frame)
            return a.frame < b.frame;
        // Downs before ups on the same frame (matches bake apply order).
        return a.down && !b.down;
    });
}

// Physics-bypass macros (MegaHack 1000–3000): keep author quantum 1:1.
// Mild FPS (60/360) still snap to live ~240 — discrete collisions match the game.
double BakeMapImplementation::resolveBakeTps(double srcTps, double liveTps) {
    if (srcTps > AssistClock::kPlanSnapTpsHi) {
        return std::min(srcTps, 4000.0);
    }
    if (liveTps < AssistClock::kPlanSnapTpsLo || liveTps > AssistClock::kPlanSnapTpsHi) {
        return AssistClock::kNominalTps;
    }
    return liveTps;
}

// Scale a ~240-tick radius/horizon to author TPS wall-time.
int BakeMapImplementation::scaleBakeTicks(int ticks, double bakeTps) {
    if (ticks <= 0)
        return ticks;
    if (bakeTps <= AssistClock::kPlanSnapTpsHi)
        return ticks;
    long long scaled =
        std::llround(static_cast<double>(ticks) * (bakeTps / AssistClock::kNominalTps));
    if (scaled < ticks)
        scaled = ticks;
    if (scaled > 2000)
        scaled = 2000;
    return static_cast<int>(scaled);
}

// Remap author FPS → bake TPS (no-op when bakeTps == srcTps), min-hold, P1-only.
std::vector<ReplayEvent>
BakeMapImplementation::prepareBakeTimeline(std::vector<ReplayEvent> const& src, double srcTps,
                                           double bakeTps, bool forceSoloP1) {
    std::vector<ReplayEvent> event = src;
    if (forceSoloP1) {
        // Keep real dual P2 only when P1 is majority; else MegaHack all-P2 → P1.
        int playerOne = 0;
        int playerTwo = 0;
        for (auto const& e : event) {
            if (e.player2)
                ++playerTwo;
            else
                ++playerOne;
        }
        if (playerTwo >= playerOne) {
            for (auto& e : event)
                e.player2 = false;
        } else {
            // Drop P2 dual clicks from P1 bake timeline (don't feed into ghost).
            event.erase(std::remove_if(event.begin(), event.end(),
                                       [](ReplayEvent const& e) { return e.player2; }),
                        event.end());
        }
    }
    if (srcTps > 0.0 && bakeTps > 0.0 && std::fabs(srcTps - bakeTps) >= 0.5) {
        float const srcDt = static_cast<float>(1.0 / srcTps);
        for (auto& e : event) {
            e.frame = AssistClock::get().remapTick(bakeTps, e.frame, srcDt);
        }
    }
    // MegaHack GDR is often 1-based (first physics tick = frame 1). Shift so the
    // opening tap lands on sim frame 0 — otherwise frame 0 falls into spawn spikes.
    {
        int minF = std::numeric_limits<int>::max();
        for (auto const& e : event) {
            if (e.frame < minF)
                minF = e.frame;
        }
        if (minF == 1) {
            for (auto& e : event)
                e.frame -= 1;
        }
    }
    BakeMapImplementation::ensureMinHoldTicks(event, 1);
    return event;
}

unsigned BakeMapImplementation::workerCount() {
    // Main-thread only: Cocos/PlayLayer are not thread-safe.
    return 1u;
}

BakeMap& BakeMap::get() {
    static BakeMap inst;
    return inst;
}

void BakeMap::clear() {
    m_data = {};
    m_loaded = false;
    this->m_dirty = false;
}

void BakeMap::clearPendingReplay() {
    m_replay = {};
    m_hasReplay = false;
}

void BakeMap::setPendingReplay(ImportedReplay replay) {
    m_replay = std::move(replay);
    m_hasReplay = true;
    AssistLog::info(fmt::format("BAKE: loaded replay {} ({} events, {:.0f} tps, {})",
                                m_replay.sourceName, m_replay.events.size(), m_replay.tps,
                                m_replay.format));
}

void BakeMap::loadForLevel(GJGameLevel* level) {
    m_data = {};
    m_loaded = false;
    this->m_dirty = false;
    if (!level)
        return;
    m_data.levelKey = LevelProfile::makeKey(level);
    auto result = readFile();
    if (result.isOk() && !m_data.clicks.empty()) {
        m_loaded = true;
        AssistLog::debug(fmt::format("BAKE: loaded map {} ({} clicks, {}, {})", m_data.levelKey,
                                     m_data.clicks.size(), m_data.sourceReplay, m_data.method));
        return;
    }
    // Empty / missing bake file — keep levelKey for next save, not a loaded map.
    m_data.sourceReplay.clear();
    m_data.method.clear();
    m_data.tps = AssistClock::kNominalTps;
    m_data.clicks.clear();
    m_loaded = false;
}

void BakeMap::save() const {
    if (!m_loaded || m_data.levelKey.empty())
        return;
    if (!this->m_dirty)
        return;
    auto result = writeFile();
    if (result.isOk()) {
        this->m_dirty = false;
        AssistLog::debug(fmt::format("BAKE: saved {} ({} clicks, {})", m_data.levelKey,
                                     m_data.clicks.size(), m_data.method));
    } else {
        AssistLog::debug(fmt::format("BAKE: save failed: {}", result.unwrapErr()));
    }
}

std::string BakeMap::statusLine() const {
    if (m_progress.running.load()) {
        return fmt::format("baking {}/{} ({:.0f}%)", m_progress.done.load(),
                           m_progress.total.load(), m_progress.fraction() * 100.f);
    }
    if (m_hasReplay && !m_data.clicks.empty()) {
        return fmt::format("{} | {} {} clicks", m_replay.sourceName, m_data.method,
                           m_data.clicks.size());
    }
    if (m_hasReplay)
        return fmt::format("{} | no bake", m_replay.sourceName);
    if (!m_data.clicks.empty()) {
        return fmt::format("{} {} clicks ({})", m_data.method, m_data.clicks.size(),
                           m_data.sourceReplay);
    }
    return "no bake";
}

std::string BakeMap::pauseStatusLine() const {
    std::string line = statusLine();
    if (m_hasReplay) {
        line = fmt::format("Replay: {} ({} ev @ {:.0f} tps)\n{}", m_replay.sourceName,
                           m_replay.events.size(), m_replay.tps, line);
    } else {
        line = fmt::format("Replay: (none) — Brute Bake needs none\n{}", line);
    }
    return line;
}

int BakeMap::refineSelfWindows(PlayLayer* layer, int startSnapshotSlot,
                               std::vector<BakedClick>& clicks, int bakeRadius, int budgetSims,
                               std::atomic<bool>* cancel) {
    return BakeWindowIndex::refineSelfWindows(*this, layer, startSnapshotSlot, clicks, bakeRadius,
                                              budgetSims, cancel);
}

std::vector<BakedClick const*> BakeMap::nextClicksAfter(float playerX, bool down, bool player2,
                                                        std::optional<GameModeKind> preferMode,
                                                        int maxCount) const {
    return BakeWindowIndex::nextClicksAfter(*this, playerX, down, player2, preferMode, maxCount);
}

int BakeMap::mergeSelfClicks(std::vector<BakedClick> const& incoming) {
    return BakeWindowIndex::mergeSelfClicks(*this, incoming);
}

float BakeMap::windowHalfW(int earliest, int latest) {
    return BakeWindowIndex::windowHalfW(earliest, latest);
}

BakedClick const* BakeMap::findNear(float playerX, int approxFrame, bool down, bool player2,
                                    std::optional<GameModeKind> preferMode) const {
    return BakeWindowIndex::findNear(*this, playerX, approxFrame, down, player2, preferMode);
}

bool BakeMap::testShiftedClick(PlayLayer* layer, int startSnapshotSlot,
                               std::vector<ReplayEvent> timeline, int eventIndex, int testFrame,
                               int untilFrame, float deltaTime, int substeps) const {
    return BakeWindowIndex::testShiftedClick(*this, layer, startSnapshotSlot, timeline, eventIndex,
                                             testFrame, untilFrame, deltaTime, substeps);
}

geode::Result<int> BakeMap::bake(PlayLayer* layer, int startSnapshotSlot, int bakeRadius,
                                 BakeMethod method) {
    if (m_progress.running.exchange(true)) {
        return geode::Err("Bake already running");
    }
    m_progress.cancel = false;
    m_progress.done = 0;
    m_progress.total = 0;
    m_progress.setStatus("starting…");

    auto finish = [&](geode::Result<int> result) -> geode::Result<int> {
        m_progress.running = false;
        m_progress.setStatus(result.isOk() ? fmt::format("done: {} clicks", result.unwrap())
                                           : fmt::format("failed: {}", result.unwrapErr()));
        return result;
    };

    if (method == BakeMethod::BruteForce) {
        return finish(bakeBrute(layer, startSnapshotSlot, bakeRadius));
    }
    return finish(bakeReplay(layer, startSnapshotSlot, bakeRadius));
}

geode::Result<int> BakeMap::bakeReplay(PlayLayer* layer, int startSnapshotSlot, int bakeRadius) {
    return BakeReplayGenerator::bakeReplay(*this, layer, startSnapshotSlot, bakeRadius);
}

geode::Result<int> BakeMap::bakeBrute(PlayLayer* layer, int startSnapshotSlot, int bakeRadius) {
    return BakeBruteGenerator::bakeBrute(*this, layer, startSnapshotSlot, bakeRadius);
}

geode::Result<> BakeMap::writeFile() const {
    (void)geode::utils::file::createDirectoryAll(BakeMapImplementation::bakesDir());
    matjson::Value root = matjson::Value::object();
    root["version"] = 1;
    root["key"] = m_data.levelKey;
    root["source"] = m_data.sourceReplay;
    root["tps"] = m_data.tps;
    root["method"] = m_data.method;

    std::vector<matjson::Value> arr;
    arr.reserve(m_data.clicks.size());
    for (auto const& c : m_data.clicks) {
        matjson::Value otherIndex = matjson::Value::object();
        otherIndex["nominal"] = c.nominalFrame;
        otherIndex["earliest"] = c.earliest;
        otherIndex["latest"] = c.latest;
        otherIndex["down"] = c.down;
        otherIndex["player2"] = c.player2;
        otherIndex["x"] = c.x;
        otherIndex["mode"] = StageConfig::kindName(c.mode);
        if (c.holdAssist)
            otherIndex["holdAssist"] = true;
        arr.push_back(std::move(otherIndex));
    }
    root["clicks"] = arr;
    return geode::utils::file::writeToJson(BakeMapImplementation::bakePath(m_data.levelKey), root);
}

geode::Result<> BakeMap::readFile() {
    auto result = geode::utils::file::readJson(BakeMapImplementation::bakePath(m_data.levelKey));
    if (result.isErr())
        return geode::Err(result.unwrapErr());
    auto root = result.unwrap();

    if (root.contains("source"))
        m_data.sourceReplay = root["source"].asString().unwrapOr("");
    if (root.contains("tps"))
        m_data.tps = root["tps"].asDouble().unwrapOr(240.0);
    if (root.contains("method"))
        m_data.method = root["method"].asString().unwrapOr("replay");
    m_data.clicks.clear();
    if (!root.contains("clicks") || !root["clicks"].isArray())
        return geode::Ok();

    for (auto const& j : root["clicks"]) {
        BakedClick c;
        c.nominalFrame = static_cast<int>(j["nominal"].asInt().unwrapOr(0));
        c.earliest = static_cast<int>(j["earliest"].asInt().unwrapOr(c.nominalFrame));
        c.latest = static_cast<int>(j["latest"].asInt().unwrapOr(c.nominalFrame));
        c.down = j["down"].asBool().unwrapOr(true);
        c.player2 = j["player2"].asBool().unwrapOr(false);
        c.x = static_cast<float>(j["x"].asDouble().unwrapOr(0.0));
        c.holdAssist = j.contains("holdAssist") ? j["holdAssist"].asBool().unwrapOr(false) : false;
        auto modeName = j.contains("mode") ? j["mode"].asString().unwrapOr("cube") : "cube";
        c.mode = GameModeKind::Cube;
        for (int index = 0; index < StageConfig::MODE_COUNT; ++index) {
            if (modeName == StageConfig::kindName(static_cast<GameModeKind>(index))) {
                c.mode = static_cast<GameModeKind>(index);
                break;
            }
        }
        m_data.clicks.push_back(c);
    }
    return geode::Ok();
}
