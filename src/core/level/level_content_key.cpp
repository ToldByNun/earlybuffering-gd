#include "level_content_key.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cstdint>
#include <cstring>

GJGameLevel* EbLevelContentKey::s_setupLevel = nullptr;
std::string EbLevelContentKey::s_setupKey;

EbLevelContentKey::SetupScope::SetupScope(GJGameLevel* level) {
    s_setupLevel = nullptr;
    s_setupKey = fullKey(level);
    s_setupLevel = level;
}

EbLevelContentKey::SetupScope::~SetupScope() {
    s_setupLevel = nullptr;
    s_setupKey.clear();
}

uint64_t EbLevelContentKeyImplementation::fnv1a(std::string const& s) {
    return fnv1aBytes(reinterpret_cast<unsigned char const*>(s.data()), s.size());
}

uint64_t EbLevelContentKeyImplementation::fnv1aBytes(unsigned char const* data, size_t length) {
    uint64_t height = 14695981039346656037ull;
    if (!data || length == 0)
        return height;
    for (size_t index = 0; index < length; ++index) {
        height ^= data[index];
        height *= 1099511628211ull;
    }
    return height;
}

uint64_t EbLevelContentKeyImplementation::fnv1aMix(uint64_t height, uint64_t value) {
    height ^= value;
    height *= 1099511628211ull;
    return height;
}

uint64_t EbLevelContentKeyImplementation::fingerprintLevelString(char const* data, size_t length) {
    // Length + head/mid/tail windows — avoid copying multi-MB level strings on enter.
    static constexpr size_t kWindow = 4096;
    uint64_t height = fnv1aMix(14695981039346656037ull, static_cast<uint64_t>(length));
    if (!data || length == 0)
        return height;

    auto mixWindow = [&](size_t offset, size_t count) {
        if (offset >= length || count == 0)
            return;
        size_t const available = std::min(count, length - offset);
        height = fnv1aMix(height, fnv1aBytes(reinterpret_cast<unsigned char const*>(data + offset),
                                             available));
    };

    mixWindow(0, kWindow);
    if (length > kWindow * 2) {
        mixWindow((length - kWindow) / 2, kWindow);
        mixWindow(length - kWindow, kWindow);
    } else if (length > kWindow) {
        mixWindow(length - kWindow, kWindow);
    }
    return height;
}

std::string EbLevelContentKey::contentRev(GJGameLevel* level) {
    if (!level)
        return "0";

    // Online / downloaded: NEVER touch m_levelString — it is often empty or
    // still decompressing at PlayLayer::init, which produced intermittent
    // fullKey values (~50% cache miss ↔ hit) and alternating enter hitches.
    if (static_cast<int>(level->m_levelID) > 0) {
        uint64_t height = 14695981039346656037ull;
        height = EbLevelContentKeyImplementation::fnv1aMix(
            height, static_cast<uint64_t>(static_cast<int>(level->m_levelID)));
        height = EbLevelContentKeyImplementation::fnv1aMix(
            height, static_cast<uint64_t>(level->m_levelVersion));
        height = EbLevelContentKeyImplementation::fnv1aMix(
            height, static_cast<uint64_t>(static_cast<int>(level->m_objectCount)));
        height = EbLevelContentKeyImplementation::fnv1aMix(
            height, static_cast<uint64_t>(level->m_levelLength));
        height = EbLevelContentKeyImplementation::fnv1aMix(
            height, static_cast<uint64_t>(level->m_updateDateUnix));
        return fmt::format("{:x}", height);
    }

    // Editor / local: prefer string fingerprint when present; else stable meta.
    try {
        auto const& body = level->m_levelString;
        size_t const length = body.size();
        char const* data = length > 0 ? body.data() : nullptr;
        if (length > 0 && data) {
            return fmt::format(
                "{:x}", EbLevelContentKeyImplementation::fingerprintLevelString(data, length));
        }
    } catch (...) {
    }
    try {
        uint64_t height = 14695981039346656037ull;
        height = EbLevelContentKeyImplementation::fnv1aMix(
            height, EbLevelContentKeyImplementation::fnv1a(std::string(level->m_levelName)));
        height = EbLevelContentKeyImplementation::fnv1aMix(
            height, static_cast<uint64_t>(level->m_levelLength));
        height = EbLevelContentKeyImplementation::fnv1aMix(
            height, static_cast<uint64_t>(static_cast<int>(level->m_objectCount)));
        height = EbLevelContentKeyImplementation::fnv1aMix(
            height, static_cast<uint64_t>(level->m_workingTime));
        return fmt::format("{:x}", height);
    } catch (...) {
        return fmt::format("{:x}", EbLevelContentKeyImplementation::fnv1a(std::string("empty")));
    }
}

std::string EbLevelContentKey::makeKey(GJGameLevel* level) {
    if (!level)
        return "unknown";
    if (level->m_levelID > 0)
        return fmt::format("id_{}", static_cast<int>(level->m_levelID));
    std::string name;
    try {
        name = std::string(level->m_levelName);
    } catch (...) {
        name = "ed";
    }
    return fmt::format("ed_{:x}", EbLevelContentKeyImplementation::fnv1a(name));
}

std::string EbLevelContentKey::fullKey(GJGameLevel* level) {
    // Reuse only within synchronous setup, where the level is immutable.
    // No pointer/length cache survives into editor changes or another entry.
    if (level && level == s_setupLevel)
        return s_setupKey;
    return fmt::format("{}_rev_{}", makeKey(level), contentRev(level));
}
