#include "trigger_world_codec.hpp"
#include "../trigger_world_store.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include <vector>

geode::Result<> EbTriggerWorldCodec::writeFile(EbTriggerWorldStore const& owner) {
    (void)geode::utils::file::createDirectoryAll(EbTriggerWorldStore::triggersDir());

    matjson::Value root = matjson::Value::object();
    root["version"] = EbTriggerWorldStore::kFileVersion;
    root["key"] = owner.m_data.levelKey;
    root["tps"] = owner.m_data.tps;

    std::vector<matjson::Value> tracksArr;
    tracksArr.reserve(owner.m_data.tracks.size());
    for (auto const& track : owner.m_data.tracks) {
        matjson::Value tj = matjson::Value::object();
        tj["uniqueId"] = track.uniqueId;
        tj["objectId"] = track.objectId;
        tj["kind"] = EbTriggerWorldStore::kindToString(track.kind);
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

    return geode::utils::file::writeToJson(EbTriggerWorldStore::triggerPath(owner.m_data.levelKey),
                                           root);
}

geode::Result<> EbTriggerWorldCodec::readFile(EbTriggerWorldStore& owner) {
    auto result =
        geode::utils::file::readJson(EbTriggerWorldStore::triggerPath(owner.m_data.levelKey));
    if (result.isErr())
        return geode::Err(result.unwrapErr());

    auto root = result.unwrap();
    int const ver = static_cast<int>(root["version"].asInt().unwrapOr(0));
    if (ver != EbTriggerWorldStore::kFileVersion) {
        owner.m_data.tracks.clear();
        return geode::Err(fmt::format("version {} != {}", ver, EbTriggerWorldStore::kFileVersion));
    }

    if (root.contains("tps")) {
        owner.m_data.tps = root["tps"].asDouble().unwrapOr(240.0);
    }
    if (owner.m_data.tps <= 0.0)
        owner.m_data.tps = 240.0;

    owner.m_data.tracks.clear();
    if (!root.contains("tracks") || !root["tracks"].isArray()) {
        return geode::Ok();
    }

    for (auto const& tj : root["tracks"]) {
        if (owner.m_data.tracks.size() >= EbTriggerWorldStore::kHardMaxTracks) {
            owner.noteSoftcap("load-hard-tracks");
            break;
        }
        if (owner.m_data.tracks.size() >= EbTriggerWorldStore::kSoftMaxTracks) {
            owner.noteSoftcap("load-soft-tracks");
            // Keep loading until hard — soft is warn, not silent stop under soft.
        }

        EbTriggerTrack track;
        track.uniqueId = static_cast<int>(tj["uniqueId"].asInt().unwrapOr(0));
        track.objectId = static_cast<int>(tj["objectId"].asInt().unwrapOr(0));
        track.kind = EbTriggerWorldStore::kindFromString(
            tj.contains("kind") ? tj["kind"].asString().unwrapOr("Ignore") : "Ignore");
        track.ready = tj["ready"].asBool().unwrapOr(false);

        if (track.uniqueId == 0)
            continue;
        // Disk may contain Dynamic* from older experiments — keep but not "ready"
        // for apply until baker decides; still load for inspection.
        if (!tj.contains("keys") || !tj["keys"].isArray()) {
            owner.m_data.tracks.push_back(std::move(track));
            continue;
        }

        for (auto const& kj : tj["keys"]) {
            if (track.keys.size() >= EbTriggerWorldStore::kHardMaxKeysPerTrack) {
                owner.noteSoftcap("load-hard-keys");
                break;
            }
            if (track.keys.size() >= EbTriggerWorldStore::kSoftMaxKeysPerTrack) {
                owner.noteSoftcap("load-soft-keys");
            }
            EbTriggerPose pose;
            pose.tick = static_cast<int>(kj["t"].asInt().unwrapOr(0));
            pose.x = static_cast<float>(kj["x"].asDouble().unwrapOr(0.0));
            pose.y = static_cast<float>(kj["y"].asDouble().unwrapOr(0.0));
            pose.rotation = static_cast<float>(kj["r"].asDouble().unwrapOr(0.0));
            pose.scaleX = static_cast<float>(kj["sx"].asDouble().unwrapOr(1.0));
            pose.scaleY = static_cast<float>(kj["sy"].asDouble().unwrapOr(1.0));
            pose.active = kj["a"].asBool().unwrapOr(true);
            track.keys.push_back(pose);
        }

        std::stable_sort(
            track.keys.begin(), track.keys.end(),
            [](EbTriggerPose const& a, EbTriggerPose const& b) { return a.tick < b.tick; });
        owner.m_data.tracks.push_back(std::move(track));
    }

    owner.m_dirty = false;
    return geode::Ok();
}
