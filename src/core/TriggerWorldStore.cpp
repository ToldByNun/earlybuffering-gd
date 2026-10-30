#include "TriggerWorldStore.hpp"
#include "AssistClock.hpp"
#include "AssistController.hpp"
#include "LevelProfile.hpp"
#include "../debug/AssistLog.hpp"

#include <Geode/utils/file.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

TriggerWorldStore& TriggerWorldStore::get() {
    static TriggerWorldStore inst;
    return inst;
}

std::filesystem::path TriggerWorldStore::triggersDir() {
    return Mod::get()->getSaveDir() / "triggers";
}

std::filesystem::path TriggerWorldStore::triggerPath(std::string const& key) {
    return TriggerWorldStore::triggersDir() / (key + ".json");
}

char const* TriggerWorldStore::kindToString(TriggerKind kind) {
    return TriggerClassify::kindName(kind);
}

TriggerKind TriggerWorldStore::kindFromString(std::string const& name) {
    if (name == "StaticMove") return TriggerKind::StaticMove;
    if (name == "StaticRotate") return TriggerKind::StaticRotate;
    if (name == "StaticScale") return TriggerKind::StaticScale;
    if (name == "StaticSpawn") return TriggerKind::StaticSpawn;
    if (name == "StaticToggle") return TriggerKind::StaticToggle;
    if (name == "DynamicFollow") return TriggerKind::DynamicFollow;
    if (name == "DynamicOther") return TriggerKind::DynamicOther;
    return TriggerKind::Ignore;
}

void TriggerWorldStore::clampData(TriggerWorldData& data) {
    if (data.tracks.size() > kMaxTracks) {
        AssistLog::info(fmt::format(
            "TRIGGER-WORLD: truncating tracks {} → {} (raise kMaxTracks)",
            data.tracks.size(),
            kMaxTracks
        ));
        data.tracks.resize(kMaxTracks);
    }
    for (auto& track : data.tracks) {
        if (track.keys.size() > kMaxKeysPerTrack) {
            track.keys.resize(kMaxKeysPerTrack);
        }
    }
}

void TriggerWorldStore::rebuildIndex() {
    this->m_index.clear();
    this->m_index.reserve(this->m_data.tracks.size() * 2);
    for (size_t i = 0; i < this->m_data.tracks.size(); ++i) {
        int const uid = this->m_data.tracks[i].uniqueId;
        if (uid == 0) continue;
        this->m_index[uid] = i;
    }
}

void TriggerWorldStore::clear() {
    this->m_data = {};
    this->m_index.clear();
    this->m_loaded = false;
    this->m_dirty = false;
    this->m_bakeComplete = false;
}

void TriggerWorldStore::clearTracks() {
    std::string const key = this->m_data.levelKey;
    double const tps = this->m_data.tps;
    int const version = this->m_data.version;
    this->m_data = {};
    this->m_data.levelKey = key;
    this->m_data.tps = tps > 0.0 ? tps : 240.0;
    this->m_data.version = version > 0 ? version : kFileVersion;
    this->m_index.clear();
    this->m_dirty = false;
    this->m_bakeComplete = false;
    this->m_loaded = !key.empty();
}

void TriggerWorldStore::loadForLevel(GJGameLevel* level) {
    this->m_data = {};
    this->m_loaded = false;
    this->m_dirty = false;
    this->m_bakeComplete = false;
    if (!level) return;

    this->m_data.levelKey = LevelProfile::makeKey(level);
    this->m_data.version = kFileVersion;
    this->m_loaded = true;

    auto res = this->readFile();
    if (res.isOk()) {
        this->rebuildIndex();
        this->m_bakeComplete = !this->m_data.tracks.empty();
        AssistLog::debug(fmt::format(
            "TRIGGER-WORLD: loaded {} ({} tracks, {} ready)",
            this->m_data.levelKey,
            this->m_data.tracks.size(),
            this->readyTrackCount()
        ));
        return;
    }
    AssistLog::debug(fmt::format(
        "TRIGGER-WORLD: no cache for {} ({})",
        this->m_data.levelKey,
        res.unwrapErr()
    ));
}

void TriggerWorldStore::save() const {
    if (!this->m_loaded || this->m_data.levelKey.empty()) return;
    if (!this->m_dirty) return;
    // Never persist cancelled / in-flight partials (poisoned cache).
    if (!this->m_bakeComplete) {
        AssistLog::debug("TRIGGER-WORLD: skip save (bake incomplete)");
        return;
    }

    auto res = this->writeFile();
    if (res.isOk()) {
        this->m_dirty = false;
        AssistLog::debug(fmt::format(
            "TRIGGER-WORLD: saved {} ({} tracks)",
            this->m_data.levelKey,
            this->m_data.tracks.size()
        ));
        return;
    }
    AssistLog::debug(fmt::format(
        "TRIGGER-WORLD: save failed: {}",
        res.unwrapErr()
    ));
}

void TriggerWorldStore::setData(TriggerWorldData data) {
    TriggerWorldStore::clampData(data);
    if (data.version <= 0) data.version = kFileVersion;
    if (data.tps <= 0.0) data.tps = 240.0;
    this->m_data = std::move(data);
    this->rebuildIndex();
    this->m_loaded = !this->m_data.levelKey.empty();
    this->m_dirty = true;
    // publishPartial mid-build — not complete until baker finishOk.
    this->m_bakeComplete = false;
}

TriggerTrack const* TriggerWorldStore::findTrack(int uniqueId) const {
    auto it = this->m_index.find(uniqueId);
    if (it == this->m_index.end()) return nullptr;
    size_t const i = it->second;
    if (i >= this->m_data.tracks.size()) return nullptr;
    return &this->m_data.tracks[i];
}

bool TriggerWorldStore::hasReadyStaticTrack(int uniqueId) const {
    auto const* track = this->findTrack(uniqueId);
    if (!track || !track->ready || track->keys.empty()) return false;
    return TriggerClassify::isStatic(track->kind);
}

size_t TriggerWorldStore::readyTrackCount() const {
    size_t n = 0;
    for (auto const& track : this->m_data.tracks) {
        if (track.ready && !track.keys.empty()) ++n;
    }
    return n;
}

int TriggerWorldStore::remapLiveTick(int liveAbsTick) const {
    return this->remapLiveTick(liveAbsTick, AssistController::get().simDt());
}

int TriggerWorldStore::remapLiveTick(int liveAbsTick, float liveSimDt) const {
    double tps = this->m_data.tps;
    if (tps <= 0.0) tps = AssistClock::kNominalTps;
    return AssistClock::get().remapTick(tps, liveAbsTick, liveSimDt);
}

bool TriggerWorldStore::samplePoseAtStoreTick(
    int uniqueId, int storeTick, TriggerPose& out
) const {
    auto const* track = this->findTrack(uniqueId);
    if (!track || !track->ready || track->keys.empty()) return false;

    // keys assumed sorted ascending by tick (baker writes them that way).
    auto const& keys = track->keys;
    // Before first key: leave capture baseline (caller skips write).
    if (storeTick < keys.front().tick) return false;
    if (storeTick >= keys.back().tick) {
        out = keys.back();
        return true;
    }

    // Segment [lo, hi): lo.tick <= storeTick < hi.tick.
    size_t hi = 0;
    for (; hi < keys.size(); ++hi) {
        if (keys[hi].tick > storeTick) break;
    }
    if (hi == 0 || hi >= keys.size()) {
        // Defensive: before-first / past-last already handled above.
        return false;
    }
    TriggerPose const& a = keys[hi - 1];
    TriggerPose const& b = keys[hi];
    if (storeTick <= a.tick || b.tick <= a.tick) {
        // Exact key or identical/inverted ticks → hold a (no lerp).
        out = a;
        return true;
    }

    float const t = static_cast<float>(storeTick - a.tick)
        / static_cast<float>(b.tick - a.tick);
    out.tick = storeTick;
    out.x = a.x + (b.x - a.x) * t;
    out.y = a.y + (b.y - a.y) * t;
    // Rotation: linear (no shortest-path wrap) — movers rarely cross ±180 mid-key.
    out.rotation = a.rotation + (b.rotation - a.rotation) * t;
    out.scaleX = a.scaleX + (b.scaleX - a.scaleX) * t;
    out.scaleY = a.scaleY + (b.scaleY - a.scaleY) * t;
    // Active hard-steps at the next key (false→true only when b is reached).
    out.active = a.active;
    return true;
}

bool TriggerWorldStore::samplePose(int uniqueId, int liveAbsTick, TriggerPose& out) const {
    int const storeTick = this->remapLiveTick(liveAbsTick);
    return this->samplePoseAtStoreTick(uniqueId, storeTick, out);
}

void TriggerWorldStore::writePose(GameObject* obj, TriggerPose const& pose) {
    if (!obj) return;
    if (!pose.active) {
        obj->setVisible(false);
        return;
    }
    obj->setVisible(true);
    obj->setPosition({ pose.x, pose.y });
    obj->m_positionX = pose.x;
    obj->m_positionY = pose.y;
    obj->setRotation(pose.rotation);
    obj->setScaleX(pose.scaleX);
    obj->setScaleY(pose.scaleY);
}

int TriggerWorldStore::applyAtTick(PlayLayer* layer, int absTick) {
    if (!layer || !this->m_loaded || !this->m_bakeComplete) return 0;
    if (this->m_data.tracks.empty() || !layer->m_objectLayer) return 0;

    // Object-centric: GD objectLayer is the live stream buffer; O(children) lookups.
    auto* children = layer->m_objectLayer->getChildren();
    if (!children) return 0;

    int applied = 0;
    unsigned const n = children->count();
    for (unsigned i = 0; i < n; ++i) {
        auto* obj = typeinfo_cast<GameObject*>(children->objectAtIndex(i));
        if (!obj) continue;
        if (!this->hasReadyStaticTrack(obj->m_uniqueID)) continue;
        TriggerPose pose{};
        if (!this->samplePose(obj->m_uniqueID, absTick, pose)) continue;
        TriggerWorldStore::writePose(obj, pose);
        ++applied;
    }
    return applied;
}

std::string TriggerWorldStore::statusLine() const {
    if (!this->m_loaded) return "trigger-world: off";
    if (this->m_data.tracks.empty()) {
        return "trigger-world: empty";
    }
    return fmt::format(
        "trigger-world: {}/{} ready @ {:.0f} tps",
        this->readyTrackCount(),
        this->m_data.tracks.size(),
        this->m_data.tps
    );
}

bool TriggerWorldStore::selfCheck(std::string& report) {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += ", ";
        report += name;
    };

    // Null-safe write (apply/restore identity without a live GameObject).
    TriggerPose dummy{};
    dummy.tick = 0;
    dummy.x = 1.f;
    dummy.y = 2.f;
    dummy.rotation = 3.f;
    dummy.scaleX = 1.f;
    dummy.scaleY = 1.f;
    dummy.active = true;
    TriggerWorldStore::writePose(nullptr, dummy);
    expect(true, "writePose-null");

    expect(TriggerClassify::classify(nullptr) == TriggerKind::Ignore, "classify-null");

    auto& store = TriggerWorldStore::get();
    store.clear();

    TriggerWorldData data;
    data.levelKey = "__selfcheck__";
    data.tps = 240.0;
    data.version = kFileVersion;

    TriggerTrack track;
    track.uniqueId = 42;
    track.objectId = 1;
    track.kind = TriggerKind::StaticMove;
    track.ready = true;
    track.keys.push_back(TriggerPose{ 10, 100.f, 200.f, 0.f, 1.f, 1.f, true });
    track.keys.push_back(TriggerPose{ 20, 150.f, 200.f, 45.f, 1.f, 1.f, true });
    data.tracks.push_back(track);

    // Not-ready track must not sample.
    TriggerTrack pending;
    pending.uniqueId = 99;
    pending.objectId = 2;
    pending.kind = TriggerKind::StaticToggle;
    pending.ready = false;
    pending.keys.push_back(TriggerPose{ 0, 0.f, 0.f, 0.f, 1.f, 1.f, false });
    data.tracks.push_back(pending);

    store.setData(std::move(data));

    TriggerPose out{};
    // Raw store-tick sampling (no live clock).
    expect(!store.samplePoseAtStoreTick(42, 5, out), "before-first");
    expect(
        store.samplePoseAtStoreTick(42, 10, out) && out.x == 100.f && out.y == 200.f,
        "at-first"
    );
    // Mid between 10→20: linear lerp (not hold-at-10).
    expect(
        store.samplePoseAtStoreTick(42, 15, out)
            && std::fabs(out.x - 125.f) < 1e-3f
            && std::fabs(out.rotation - 22.5f) < 1e-3f
            && out.active,
        "lerp-mid"
    );
    expect(
        store.samplePoseAtStoreTick(42, 20, out) && out.x == 150.f && out.rotation == 45.f,
        "at-last"
    );
    expect(store.samplePoseAtStoreTick(42, 99, out) && out.x == 150.f, "past-last-hold");
    expect(!store.samplePoseAtStoreTick(999, 10, out), "missing-id");
    expect(!store.samplePoseAtStoreTick(99, 0, out), "not-ready");

    // Active hard-step: false at a, true at b → mid stays false until next key.
    {
        TriggerTrack step;
        step.uniqueId = 77;
        step.objectId = 3;
        step.kind = TriggerKind::StaticToggle;
        step.ready = true;
        step.keys.push_back(TriggerPose{ 0, 0.f, 0.f, 0.f, 1.f, 1.f, false });
        step.keys.push_back(TriggerPose{ 10, 10.f, 0.f, 0.f, 1.f, 1.f, true });
        store.m_data.tracks.push_back(std::move(step));
        store.rebuildIndex();
        expect(
            store.samplePoseAtStoreTick(77, 5, out) && !out.active
                && std::fabs(out.x - 5.f) < 1e-3f,
            "active-hard-step-mid"
        );
        expect(
            store.samplePoseAtStoreTick(77, 10, out) && out.active && out.x == 10.f,
            "active-hard-step-at-b"
        );
        // Drop temp track so ready-count stays on the StaticMove fixture.
        store.m_data.tracks.pop_back();
        store.rebuildIndex();
    }

    // Remap: store @ 240tps, live simDt = 1/120 → live 100 → store 200.
    expect(store.remapLiveTick(100, 1.f / 240.f) == 100, "remap-identity");
    expect(store.remapLiveTick(100, 1.f / 120.f) == 200, "remap-2x-live-dt");
    // Hitch dt must not blow store ticks (snap to nominal → identity @ 240).
    expect(store.remapLiveTick(100, 0.25f) == 100, "remap-insane-snap");
    // live 10 @ 2x dt → store 20 → last key pose.
    expect(
        store.samplePoseAtStoreTick(42, store.remapLiveTick(10, 1.f / 120.f), out)
            && out.x == 150.f,
        "remap-then-sample"
    );
    // live 10 @ nominal → store 10 → first key.
    expect(
        store.samplePoseAtStoreTick(42, store.remapLiveTick(10, 1.f / 240.f), out)
            && out.x == 100.f,
        "remap-id-sample"
    );

    expect(store.readyTrackCount() == 1, "ready-count");
    expect(TriggerClassify::isStatic(TriggerKind::StaticToggle), "static-toggle");
    expect(
        std::string(TriggerClassify::kindName(TriggerKind::StaticToggle)) == "StaticToggle",
        "kind-name-toggle"
    );

    store.clear();
    expect(!store.loaded() && !store.hasTracks(), "cleared");

    if (fails == 0) {
        report = "TriggerWorldStore selfCheck OK";
        return true;
    }
    report = fmt::format("TriggerWorldStore selfCheck {} fails: {}", fails, report);
    return false;
}

Result<> TriggerWorldStore::writeFile() const {
    (void)file::createDirectoryAll(TriggerWorldStore::triggersDir());

    matjson::Value root = matjson::Value::object();
    root["version"] = kFileVersion;
    root["key"] = this->m_data.levelKey;
    root["tps"] = this->m_data.tps;

    std::vector<matjson::Value> tracksArr;
    tracksArr.reserve(this->m_data.tracks.size());
    for (auto const& track : this->m_data.tracks) {
        matjson::Value tj = matjson::Value::object();
        tj["uniqueId"] = track.uniqueId;
        tj["objectId"] = track.objectId;
        tj["kind"] = TriggerWorldStore::kindToString(track.kind);
        tj["ready"] = track.ready;

        std::vector<matjson::Value> keysArr;
        keysArr.reserve(track.keys.size());
        for (auto const& pose : track.keys) {
            matjson::Value kj = matjson::Value::object();
            kj["t"] = pose.tick;
            kj["x"] = pose.x;
            kj["y"] = pose.y;
            kj["r"] = pose.rotation;
            kj["sx"] = pose.scaleX;
            kj["sy"] = pose.scaleY;
            kj["a"] = pose.active;
            keysArr.push_back(std::move(kj));
        }
        tj["keys"] = keysArr;
        tracksArr.push_back(std::move(tj));
    }
    root["tracks"] = tracksArr;

    return file::writeToJson(
        TriggerWorldStore::triggerPath(this->m_data.levelKey),
        root
    );
}

Result<> TriggerWorldStore::readFile() {
    auto res = file::readJson(TriggerWorldStore::triggerPath(this->m_data.levelKey));
    if (res.isErr()) return Err(res.unwrapErr());

    auto root = res.unwrap();
    int const ver = static_cast<int>(root["version"].asInt().unwrapOr(0));
    if (ver != kFileVersion) {
        this->m_data.tracks.clear();
        return Err(fmt::format("version {} != {}", ver, kFileVersion));
    }

    if (root.contains("tps")) {
        this->m_data.tps = root["tps"].asDouble().unwrapOr(240.0);
    }
    if (this->m_data.tps <= 0.0) this->m_data.tps = 240.0;

    this->m_data.tracks.clear();
    if (!root.contains("tracks") || !root["tracks"].isArray()) {
        return Ok();
    }

    for (auto const& tj : root["tracks"]) {
        if (this->m_data.tracks.size() >= kMaxTracks) break;

        TriggerTrack track;
        track.uniqueId = static_cast<int>(tj["uniqueId"].asInt().unwrapOr(0));
        track.objectId = static_cast<int>(tj["objectId"].asInt().unwrapOr(0));
        track.kind = TriggerWorldStore::kindFromString(
            tj.contains("kind") ? tj["kind"].asString().unwrapOr("Ignore") : "Ignore"
        );
        track.ready = tj["ready"].asBool().unwrapOr(false);

        if (track.uniqueId == 0) continue;
        // Disk may contain Dynamic* from older experiments — keep but not "ready"
        // for apply until baker decides; still load for inspection.
        if (!tj.contains("keys") || !tj["keys"].isArray()) {
            this->m_data.tracks.push_back(std::move(track));
            continue;
        }

        for (auto const& kj : tj["keys"]) {
            if (track.keys.size() >= kMaxKeysPerTrack) break;
            TriggerPose pose;
            pose.tick = static_cast<int>(kj["t"].asInt().unwrapOr(0));
            pose.x = static_cast<float>(kj["x"].asDouble().unwrapOr(0.0));
            pose.y = static_cast<float>(kj["y"].asDouble().unwrapOr(0.0));
            pose.rotation = static_cast<float>(kj["r"].asDouble().unwrapOr(0.0));
            pose.scaleX = static_cast<float>(kj["sx"].asDouble().unwrapOr(1.0));
            pose.scaleY = static_cast<float>(kj["sy"].asDouble().unwrapOr(1.0));
            pose.active = kj["a"].asBool().unwrapOr(true);
            track.keys.push_back(pose);
        }

        std::stable_sort(track.keys.begin(), track.keys.end(),
            [](TriggerPose const& a, TriggerPose const& b) {
                return a.tick < b.tick;
            }
        );
        this->m_data.tracks.push_back(std::move(track));
    }

    this->m_dirty = false;
    return Ok();
}
