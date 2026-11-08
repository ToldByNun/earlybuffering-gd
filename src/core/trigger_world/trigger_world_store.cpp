#include "trigger_world_store.hpp"
#include "../../debug/assist_log.hpp"
#include "../assist/assist_clock.hpp"
#include "../assist/assist_controller.hpp"
#include "../learning/level_profile.hpp"
#include "storage/trigger_world_codec.hpp"
#include "trigger_classify.hpp"
#include "validation/trigger_world_checks.hpp"
#include <Geode/utils/file.hpp>
#include <filesystem>
#include <fmt/format.h>
#include <string>
#include <vector>

TriggerWorldStore& TriggerWorldStore::get() {
    static TriggerWorldStore inst;
    return inst;
}

std::filesystem::path TriggerWorldStore::triggersDir() {
    return geode::prelude::Mod::get()->getSaveDir() / "triggers";
}

std::filesystem::path TriggerWorldStore::triggerPath(std::string const& key) {
    return TriggerWorldStore::triggersDir() / (key + ".json");
}

char const* TriggerWorldStore::kindToString(TriggerKind kind) {
    return TriggerClassify::kindName(kind);
}

TriggerKind TriggerWorldStore::kindFromString(std::string const& name) {
    if (name == "StaticMove")
        return TriggerKind::StaticMove;
    if (name == "StaticRotate")
        return TriggerKind::StaticRotate;
    if (name == "StaticScale")
        return TriggerKind::StaticScale;
    if (name == "StaticSpawn")
        return TriggerKind::StaticSpawn;
    if (name == "StaticToggle")
        return TriggerKind::StaticToggle;
    if (name == "DynamicFollow")
        return TriggerKind::DynamicFollow;
    if (!(name == "DynamicOther")) {
        return TriggerKind::Ignore;
    }
    return TriggerKind::DynamicOther;
}

void TriggerWorldStore::decimateKeys(std::vector<TriggerPose>& keys, size_t maxKeep) {
    if (keys.size() <= maxKeep || maxKeep < 2)
        return;
    std::vector<TriggerPose> out;
    out.reserve(maxKeep);
    out.push_back(keys.front());
    size_t const midSlots = maxKeep - 2;
    size_t const inner = keys.size() - 2;
    for (size_t index = 0; index < midSlots && inner > 0; ++index) {
        size_t indexValue = 1 + (index * inner) / midSlots;
        if (indexValue >= keys.size() - 1)
            indexValue = keys.size() - 2;
        if (out.back().tick == keys[indexValue].tick)
            continue;
        out.push_back(keys[indexValue]);
    }
    if (out.back().tick != keys.back().tick) {
        out.push_back(keys.back());
    } else {
        out.back() = keys.back();
    }
    keys = std::move(out);
}

TriggerWorldStore::ClampResult TriggerWorldStore::clampData(TriggerWorldData& data) {
    ClampResult r;
    if (data.tracks.size() >= kSoftMaxTracks) {
        r.softTracks = true;
    }
    if (data.tracks.size() > kHardMaxTracks) {
        r.hardTracks = true;
        data.tracks.resize(kHardMaxTracks);
    }
    for (auto& track : data.tracks) {
        if (track.keys.size() >= kSoftMaxKeysPerTrack) {
            r.softKeys = true;
        }
        if (track.keys.size() > kHardMaxKeysPerTrack) {
            r.hardKeys = true;
            TriggerWorldStore::decimateKeys(track.keys, kHardMaxKeysPerTrack);
        }
    }
    return r;
}

void TriggerWorldStore::noteSoftcap(char const* where) {
    this->m_softcapHit = true;
    if (this->m_softcapLogged)
        return;
    this->m_softcapLogged = true;
    AssistLog::info(fmt::format("TRIGGER-WORLD: softcap ({}) tracks≤{} keys/track≤{} — refusing "
                                "further growth; no silent drop under soft",
                                where ? where : "?", kSoftMaxTracks, kSoftMaxKeysPerTrack));
}

void TriggerWorldStore::clear() {
    this->m_data = {};
    this->m_loaded = false;
    this->m_dirty = false;
    this->m_softcapHit = false;
    this->m_softcapLogged = false;
}

void TriggerWorldStore::loadForLevel(GJGameLevel* level) {
    this->m_data = {};
    this->m_loaded = false;
    this->m_dirty = false;
    this->m_softcapHit = false;
    this->m_softcapLogged = false;
    if (!level)
        return;

    this->m_data.levelKey = LevelProfile::makeKey(level);
    this->m_data.version = kFileVersion;
    this->m_loaded = true;

    auto result = this->readFile();
    if (result.isOk()) {
        // Commit 4: empty on-disk cache is a miss (init raced → poisoned file).
        if (this->m_data.tracks.empty()) {
            AssistLog::info(
                fmt::format("TRIGGER-WORLD: empty cache for {} — treat as miss (rebuild)",
                            this->m_data.levelKey));
            this->discardCacheFile();
            this->m_dirty = false;
            return;
        }
        AssistLog::debug(fmt::format("TRIGGER-WORLD: loaded {} ({} tracks, {} ready)",
                                     this->m_data.levelKey, this->m_data.tracks.size(),
                                     this->readyTrackCount()));
        return;
    }
    AssistLog::debug(fmt::format("TRIGGER-WORLD: no cache for {} ({})", this->m_data.levelKey,
                                 result.unwrapErr()));
}

void TriggerWorldStore::save() const {
    if (!this->m_loaded || this->m_data.levelKey.empty())
        return;
    if (!this->m_dirty)
        return;

    auto result = this->writeFile();
    if (result.isOk()) {
        this->m_dirty = false;
        AssistLog::debug(fmt::format("TRIGGER-WORLD: saved {} ({} tracks)", this->m_data.levelKey,
                                     this->m_data.tracks.size()));
        return;
    }
    AssistLog::debug(fmt::format("TRIGGER-WORLD: save failed: {}", result.unwrapErr()));
}

void TriggerWorldStore::setData(TriggerWorldData data) {
    auto const clamp = TriggerWorldStore::clampData(data);
    if (clamp.softTracks || clamp.softKeys) {
        this->noteSoftcap(clamp.hardTracks || clamp.hardKeys ? "hard-clamp" : "setData");
    }
    if (clamp.hardTracks || clamp.hardKeys) {
        AssistLog::info(fmt::format(
            "TRIGGER-WORLD: HARD truncate tracks={} keysHit={} (hard tracks≤{} keys≤{})",
            clamp.hardTracks ? 1 : 0, clamp.hardKeys ? 1 : 0, kHardMaxTracks,
            kHardMaxKeysPerTrack));
    }
    if (data.version <= 0)
        data.version = kFileVersion;
    if (data.tps <= 0.0)
        data.tps = 240.0;
    this->m_data = std::move(data);
    this->m_loaded = !this->m_data.levelKey.empty();
    this->m_dirty = true;
}

void TriggerWorldStore::discardCacheFile() const {
    if (this->m_data.levelKey.empty())
        return;
    auto const path = TriggerWorldStore::triggerPath(this->m_data.levelKey);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return;
    std::filesystem::remove(path, ec);
    if (ec) {
        AssistLog::debug(fmt::format("TRIGGER-WORLD: discard cache failed: {}", ec.message()));
        return;
    }
    AssistLog::info(
        fmt::format("TRIGGER-WORLD: discarded empty/stale cache {}", path.filename().string()));
}

TriggerTrack const* TriggerWorldStore::findTrack(int uniqueId) const {
    for (auto const& track : this->m_data.tracks) {
        if (track.uniqueId == uniqueId)
            return &track;
    }
    return nullptr;
}

size_t TriggerWorldStore::readyTrackCount() const {
    size_t count = 0;
    for (auto const& track : this->m_data.tracks) {
        if (track.ready && !track.keys.empty())
            ++count;
    }
    return count;
}

int TriggerWorldStore::remapLiveTick(int liveAbsTick) const {
    return this->remapLiveTick(liveAbsTick, AssistController::get().simDt());
}

int TriggerWorldStore::remapLiveTick(int liveAbsTick, float liveSimDt) const {
    double tps = this->m_data.tps;
    if (tps <= 0.0)
        tps = AssistClock::kNominalTps;
    return AssistClock::get().remapTick(tps, liveAbsTick, liveSimDt);
}

bool TriggerWorldStore::samplePoseAtStoreTick(int uniqueId, int storeTick, TriggerPose& out) const {
    auto const* track = this->findTrack(uniqueId);
    if (!track || !track->ready || track->keys.empty())
        return false;

    // keys assumed sorted ascending by tick (baker writes them that way).
    auto const& keys = track->keys;
    // Before first key: leave capture baseline (caller skips write).
    if (storeTick < keys.front().tick)
        return false;
    if (storeTick >= keys.back().tick) {
        out = keys.back();
        return true;
    }

    // Last key with tick <= storeTick (hold). Linear lerp optional later.
    TriggerPose const* best = &keys.front();
    for (auto const& key : keys) {
        if (key.tick > storeTick)
            break;
        best = &key;
    }
    out = *best;
    return true;
}

bool TriggerWorldStore::samplePose(int uniqueId, int liveAbsTick, float liveSimDt,
                                   TriggerPose& out) const {
    int const storeTick = this->remapLiveTick(liveAbsTick, liveSimDt);
    return this->samplePoseAtStoreTick(uniqueId, storeTick, out);
}

bool TriggerWorldStore::samplePose(int uniqueId, int liveAbsTick, TriggerPose& out) const {
    return this->samplePose(uniqueId, liveAbsTick, AssistController::get().simDt(), out);
}

void TriggerWorldStore::writePose(GameObject* object, TriggerPose const& pose) {
    if (!object)
        return;
    auto* effectObject = geode::prelude::typeinfo_cast<EnhancedGameObject*>(object);
    // Inactive: disable collision participation, not visibility alone
    // (invisible solids still killed through Include — Commit 4).
    if (!pose.active) {
        if (effectObject) {
            effectObject->m_poweredOn = false;
            effectObject->m_visible = false;
        }
        object->setVisible(false);
        return;
    }
    if (effectObject) {
        effectObject->m_poweredOn = true;
        effectObject->m_visible = true;
    }
    object->setVisible(true);
    object->setPosition({pose.x, pose.y});
    object->m_positionX = pose.x;
    object->m_positionY = pose.y;
    object->setRotation(pose.rotation);
    object->setScaleX(pose.scaleX);
    object->setScaleY(pose.scaleY);
}

int TriggerWorldStore::applyAtTick(PlayLayer* layer, int absTick) {
    if (!layer || !this->m_loaded || this->m_data.tracks.empty())
        return 0;
    if (!layer->m_objectLayer)
        return 0;

    // Build uniqueId → object once per call (caller may prefer ShadowSimulator's
    // capture list for the hot path — see applyTriggerWorldAtTick).
    auto* children = layer->m_objectLayer->getChildren();
    if (!children)
        return 0;

    int applied = 0;
    for (auto const& track : this->m_data.tracks) {
        if (!track.ready || track.keys.empty())
            continue;
        if (!TriggerClassify::isStatic(track.kind))
            continue;

        TriggerPose pose{};
        if (!this->samplePose(track.uniqueId, absTick, pose))
            continue;

        GameObject* object = nullptr;
        for (unsigned index = 0; index < children->count(); ++index) {
            auto* candidate =
                geode::prelude::typeinfo_cast<GameObject*>(children->objectAtIndex(index));
            if (!candidate)
                continue;
            if (candidate->m_uniqueID != track.uniqueId)
                continue;
            object = candidate;
            break;
        }
        if (!object)
            continue;

        TriggerWorldStore::writePose(object, pose);
        ++applied;
    }
    return applied;
}

std::string TriggerWorldStore::statusLine() const {
    if (!this->m_loaded)
        return "trigger-world: off";
    if (this->m_data.tracks.empty()) {
        return "trigger-world: empty";
    }
    if (this->m_softcapHit) {
        return fmt::format("trigger-world: {}/{} ready @ {:.0f} tps softcap",
                           this->readyTrackCount(), this->m_data.tracks.size(), this->m_data.tps);
    }
    return fmt::format("trigger-world: {}/{} ready @ {:.0f} tps", this->readyTrackCount(),
                       this->m_data.tracks.size(), this->m_data.tps);
}

bool TriggerWorldStore::selfCheck(std::string& report) {
    return TriggerWorldChecks::selfCheck(report);
}

geode::Result<> TriggerWorldStore::writeFile() const {
    return TriggerWorldCodec::writeFile(*this);
}

geode::Result<> TriggerWorldStore::readFile() {
    return TriggerWorldCodec::readFile(*this);
}
