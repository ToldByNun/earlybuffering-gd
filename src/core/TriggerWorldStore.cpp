#include "TriggerWorldStore.hpp"
#include "LevelProfile.hpp"
#include "../debug/AssistLog.hpp"

#include <Geode/utils/file.hpp>
#include <fmt/format.h>

#include <algorithm>

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
    if (name == "DynamicFollow") return TriggerKind::DynamicFollow;
    if (name == "DynamicOther") return TriggerKind::DynamicOther;
    return TriggerKind::Ignore;
}

void TriggerWorldStore::clampData(TriggerWorldData& data) {
    if (data.tracks.size() > kMaxTracks) {
        data.tracks.resize(kMaxTracks);
    }
    for (auto& track : data.tracks) {
        if (track.keys.size() > kMaxKeysPerTrack) {
            track.keys.resize(kMaxKeysPerTrack);
        }
    }
}

void TriggerWorldStore::clear() {
    this->m_data = {};
    this->m_loaded = false;
    this->m_dirty = false;
}

void TriggerWorldStore::loadForLevel(GJGameLevel* level) {
    this->m_data = {};
    this->m_loaded = false;
    this->m_dirty = false;
    if (!level) return;

    this->m_data.levelKey = LevelProfile::makeKey(level);
    this->m_data.version = kFileVersion;
    this->m_loaded = true;

    auto res = this->readFile();
    if (res.isOk()) {
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
    this->m_loaded = !this->m_data.levelKey.empty();
    this->m_dirty = true;
}

TriggerTrack const* TriggerWorldStore::findTrack(int uniqueId) const {
    for (auto const& track : this->m_data.tracks) {
        if (track.uniqueId == uniqueId) return &track;
    }
    return nullptr;
}

size_t TriggerWorldStore::readyTrackCount() const {
    size_t n = 0;
    for (auto const& track : this->m_data.tracks) {
        if (track.ready && !track.keys.empty()) ++n;
    }
    return n;
}

bool TriggerWorldStore::samplePose(int uniqueId, int absTick, TriggerPose& out) const {
    auto const* track = this->findTrack(uniqueId);
    if (!track || !track->ready || track->keys.empty()) return false;

    // keys assumed sorted ascending by tick (baker writes them that way).
    auto const& keys = track->keys;
    if (absTick <= keys.front().tick) {
        out = keys.front();
        return true;
    }
    if (absTick >= keys.back().tick) {
        out = keys.back();
        return true;
    }

    // Last key with tick <= absTick (hold). Linear lerp optional later.
    TriggerPose const* best = &keys.front();
    for (auto const& key : keys) {
        if (key.tick > absTick) break;
        best = &key;
    }
    out = *best;
    return true;
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
