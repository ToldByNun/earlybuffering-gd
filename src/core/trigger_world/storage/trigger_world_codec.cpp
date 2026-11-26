#include "trigger_world_codec.hpp"
#include "../../../debug/assist_log.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <asp/fs/std.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <vector>

std::filesystem::path EbTriggerWorldCodec::binaryPath(std::string const& key) {
    return EbTriggerWorldStore::triggersDir() / (key + ".ebtw");
}

std::filesystem::path EbTriggerWorldCodec::jsonPath(std::string const& key) {
    return EbTriggerWorldStore::triggersDir() / (key + ".json");
}

template <typename T>
static bool ebTwWritePod(std::ofstream& out, T const& value) {
    out.write(reinterpret_cast<char const*>(&value), sizeof(T));
    return static_cast<bool>(out);
}

template <typename T>
static bool ebTwReadPod(std::ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(in);
}

static std::uint8_t ebTwKindToByte(TriggerKind kind) {
    return static_cast<std::uint8_t>(kind);
}

static TriggerKind ebTwKindFromByte(std::uint8_t value) {
    if (value > static_cast<std::uint8_t>(TriggerKind::DynamicOther))
        return TriggerKind::Ignore;
    return static_cast<TriggerKind>(value);
}

geode::Result<> EbTriggerWorldCodec::writeBinary(std::string const& key,
                                                 EbTriggerWorldData const& data) {
    (void)geode::utils::file::createDirectoryAll(
        asp::fs::Path::fromStdPath(EbTriggerWorldStore::triggersDir()));

    auto const path = binaryPath(key);
    auto const tmp = path.string() + ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out)
        return geode::Err(fmt::format("open write failed: {}", tmp));

    char const magic[4] = {'E', 'B', 'T', 'W'};
    out.write(magic, 4);
    if (!ebTwWritePod(out, kBinaryVersion))
        return geode::Err("write binVer failed");
    std::uint32_t const compat = static_cast<std::uint32_t>(EbTriggerWorldStore::kFileVersion);
    if (!ebTwWritePod(out, compat))
        return geode::Err("write compatVer failed");
    if (!ebTwWritePod(out, data.tps))
        return geode::Err("write tps failed");

    std::uint32_t flags = 0;
    if (data.emptyComplete)
        flags |= 1u;
    if (data.emptySkipDynamic)
        flags |= 2u;
    if (!ebTwWritePod(out, flags))
        return geode::Err("write flags failed");
    std::uint32_t const emptyObjects = static_cast<std::uint32_t>(data.emptyObjectCount);
    if (!ebTwWritePod(out, emptyObjects))
        return geode::Err("write emptyObjects failed");

    std::uint32_t const trackCount = static_cast<std::uint32_t>(data.tracks.size());
    if (!ebTwWritePod(out, trackCount))
        return geode::Err("write trackCount failed");

    for (auto const& track : data.tracks) {
        std::int32_t const uniqueId = track.uniqueId;
        std::int32_t const objectId = track.objectId;
        std::uint8_t const kind = ebTwKindToByte(track.kind);
        std::uint8_t const ready = track.ready ? 1u : 0u;
        std::uint16_t const pad = 0;
        std::uint32_t const keyCount = static_cast<std::uint32_t>(track.keys.size());
        if (!ebTwWritePod(out, uniqueId) || !ebTwWritePod(out, objectId) ||
            !ebTwWritePod(out, kind) || !ebTwWritePod(out, ready) || !ebTwWritePod(out, pad) ||
            !ebTwWritePod(out, keyCount))
            return geode::Err("write track header failed");
        for (auto const& pose : track.keys) {
            std::int32_t const tick = pose.tick;
            std::uint8_t const active = pose.active ? 1u : 0u;
            std::uint8_t const pad3[3] = {0, 0, 0};
            if (!ebTwWritePod(out, tick) || !ebTwWritePod(out, pose.x) || !ebTwWritePod(out, pose.y) ||
                !ebTwWritePod(out, pose.rotation) || !ebTwWritePod(out, pose.scaleX) ||
                !ebTwWritePod(out, pose.scaleY) || !ebTwWritePod(out, active) ||
                !out.write(reinterpret_cast<char const*>(pad3), 3))
                return geode::Err("write key failed");
        }
    }
    out.close();
    if (!out)
        return geode::Err("flush binary failed");

    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
        if (ec)
            return geode::Err(fmt::format("rename binary failed: {}", ec.message()));
    }
    return geode::Ok();
}

geode::Result<> EbTriggerWorldCodec::readBinary(std::string const& key,
                                                EbTriggerWorldData& outData) {
    auto const path = binaryPath(key);
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return geode::Err("no binary");

    char magic[4] = {};
    char const expect[4] = {'E', 'B', 'T', 'W'};
    in.read(magic, 4);
    if (!in || std::memcmp(magic, expect, 4) != 0)
        return geode::Err("bad magic");

    std::uint32_t binVer = 0;
    std::uint32_t compat = 0;
    if (!ebTwReadPod(in, binVer) || !ebTwReadPod(in, compat))
        return geode::Err("header truncated");
    if (binVer != kBinaryVersion)
        return geode::Err(fmt::format("binVer {} != {}", binVer, kBinaryVersion));
    if (static_cast<int>(compat) != EbTriggerWorldStore::kFileVersion)
        return geode::Err(fmt::format("compat {} != {}", compat, EbTriggerWorldStore::kFileVersion));

    outData = {};
    outData.levelKey = key;
    outData.version = EbTriggerWorldStore::kFileVersion;
    if (!ebTwReadPod(in, outData.tps))
        return geode::Err("tps truncated");
    if (outData.tps <= 0.0)
        outData.tps = 240.0;

    std::uint32_t flags = 0;
    std::uint32_t emptyObjects = 0;
    std::uint32_t trackCount = 0;
    if (!ebTwReadPod(in, flags) || !ebTwReadPod(in, emptyObjects) || !ebTwReadPod(in, trackCount))
        return geode::Err("meta truncated");
    outData.emptyComplete = (flags & 1u) != 0;
    outData.emptySkipDynamic = (flags & 2u) != 0;
    outData.emptyObjectCount = emptyObjects;

    if (trackCount > EbTriggerWorldStore::kHardMaxTracks)
        return geode::Err("trackCount insane");
    outData.tracks.reserve(trackCount);

    for (std::uint32_t trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
        std::int32_t uniqueId = 0;
        std::int32_t objectId = 0;
        std::uint8_t kind = 0;
        std::uint8_t ready = 0;
        std::uint16_t pad = 0;
        std::uint32_t keyCount = 0;
        if (!ebTwReadPod(in, uniqueId) || !ebTwReadPod(in, objectId) || !ebTwReadPod(in, kind) ||
            !ebTwReadPod(in, ready) || !ebTwReadPod(in, pad) || !ebTwReadPod(in, keyCount))
            return geode::Err("track header truncated");
        if (keyCount > EbTriggerWorldStore::kHardMaxKeysPerTrack)
            return geode::Err("keyCount insane");

        EbTriggerTrack track;
        track.uniqueId = uniqueId;
        track.objectId = objectId;
        track.kind = ebTwKindFromByte(kind);
        track.ready = ready != 0;
        track.keys.resize(keyCount);
        for (std::uint32_t keyIndex = 0; keyIndex < keyCount; ++keyIndex) {
            std::int32_t tick = 0;
            std::uint8_t active = 1;
            char pad3[3] = {};
            auto& pose = track.keys[keyIndex];
            if (!ebTwReadPod(in, tick) || !ebTwReadPod(in, pose.x) || !ebTwReadPod(in, pose.y) ||
                !ebTwReadPod(in, pose.rotation) || !ebTwReadPod(in, pose.scaleX) ||
                !ebTwReadPod(in, pose.scaleY) || !ebTwReadPod(in, active) || !in.read(pad3, 3))
                return geode::Err("key truncated");
            pose.tick = tick;
            pose.active = active != 0;
        }
        if (track.uniqueId == 0)
            continue;
        outData.tracks.push_back(std::move(track));
    }
    return geode::Ok();
}

geode::Result<> EbTriggerWorldCodec::readJson(std::string const& key, EbTriggerWorldData& outData) {
    auto result = geode::utils::file::readJson(asp::fs::Path::fromStdPath(jsonPath(key)));
    if (result.isErr())
        return geode::Err(result.unwrapErr());

    auto root = result.unwrap();
    int const ver = static_cast<int>(root["version"].asInt().unwrapOr(0));
    if (ver != EbTriggerWorldStore::kFileVersion) {
        return geode::Err(fmt::format("version {} != {}", ver, EbTriggerWorldStore::kFileVersion));
    }

    outData = {};
    outData.levelKey = key;
    outData.version = EbTriggerWorldStore::kFileVersion;
    if (root.contains("tps"))
        outData.tps = root["tps"].asDouble().unwrapOr(240.0);
    if (outData.tps <= 0.0)
        outData.tps = 240.0;

    outData.emptyComplete =
        root.contains("emptyComplete") ? root["emptyComplete"].asBool().unwrapOr(false) : false;
    outData.emptyObjectCount =
        root.contains("emptyObjectCount")
            ? static_cast<size_t>(
                  std::max(0, static_cast<int>(root["emptyObjectCount"].asInt().unwrapOr(0))))
            : 0;
    outData.emptySkipDynamic = root.contains("emptySkipDynamic")
                                   ? root["emptySkipDynamic"].asBool().unwrapOr(false)
                                   : false;

    if (!root.contains("tracks") || !root["tracks"].isArray())
        return geode::Ok();

    for (auto const& tj : root["tracks"]) {
        if (outData.tracks.size() >= EbTriggerWorldStore::kHardMaxTracks)
            break;

        EbTriggerTrack track;
        track.uniqueId = static_cast<int>(tj["uniqueId"].asInt().unwrapOr(0));
        track.objectId = static_cast<int>(tj["objectId"].asInt().unwrapOr(0));
        track.kind = EbTriggerWorldStore::kindFromString(
            tj.contains("kind") ? tj["kind"].asString().unwrapOr("Ignore") : "Ignore");
        track.ready = tj["ready"].asBool().unwrapOr(false);
        if (track.uniqueId == 0)
            continue;

        if (tj.contains("keys") && tj["keys"].isArray()) {
            for (auto const& kj : tj["keys"]) {
                if (track.keys.size() >= EbTriggerWorldStore::kHardMaxKeysPerTrack)
                    break;
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
        }
        outData.tracks.push_back(std::move(track));
    }
    return geode::Ok();
}

geode::Result<> EbTriggerWorldCodec::loadInto(std::string const& key, EbTriggerWorldData& outData,
                                              bool& fromJson, std::string& sourceTag) {
    fromJson = false;
    sourceTag = "miss";

    auto bin = readBinary(key, outData);
    if (bin.isOk()) {
        sourceTag = "binary";
        return geode::Ok();
    }

    auto json = readJson(key, outData);
    if (json.isErr())
        return geode::Err(json.unwrapErr());

    fromJson = true;
    sourceTag = "json";
    // Migrate off 100MB+ JSON so the next enter is a fast binary read.
    auto written = writeBinary(key, outData);
    if (written.isOk()) {
        std::error_code ec;
        std::filesystem::remove(jsonPath(key), ec);
        sourceTag = "json->binary";
        EbAssistLog::info(fmt::format(
            "TRIGGER-WORLD: migrated {} JSON → .ebtw ({} tracks)", key, outData.tracks.size()));
    } else {
        EbAssistLog::info(
            fmt::format("TRIGGER-WORLD: JSON load ok but binary migrate failed: {}",
                        written.unwrapErr()));
    }
    return geode::Ok();
}

geode::Result<> EbTriggerWorldCodec::writeFile(EbTriggerWorldStore const& owner) {
    auto result = writeBinary(owner.m_data.levelKey, owner.m_data);
    if (result.isOk()) {
        // Drop legacy JSON once binary is authoritative.
        std::error_code ec;
        std::filesystem::remove(jsonPath(owner.m_data.levelKey), ec);
    }
    return result;
}

geode::Result<> EbTriggerWorldCodec::readFile(EbTriggerWorldStore& owner) {
    bool fromJson = false;
    std::string sourceTag;
    auto result = loadInto(owner.m_data.levelKey, owner.m_data, fromJson, sourceTag);
    (void)fromJson;
    (void)sourceTag;
    return result;
}
