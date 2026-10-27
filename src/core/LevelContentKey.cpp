#include "LevelContentKey.hpp"
#include <Geode/Geode.hpp>
#include <cstdint>
using namespace geode::prelude;

static uint64_t fnv1a(std::string const& s) {
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string LevelContentKey::contentRev(GJGameLevel* level) {
    if (!level) return "0";
    std::string body;
    try { body = level->m_levelString; } catch (...) { body.clear(); }
    if (body.empty()) {
        try { body = level->m_levelName + std::to_string(level->m_levelLength); } catch (...) { body = "empty"; }
    }
    // stream-ish: hash in chunks conceptually (full string ok for typical levels)
    return fmt::format("{:x}", fnv1a(body));
}

std::string LevelContentKey::makeKey(GJGameLevel* level) {
    if (!level) return "unknown";
    if (level->m_levelID > 0) return fmt::format("id_{}", static_cast<int>(level->m_levelID));
    std::string name;
    try { name = level->m_levelName; } catch (...) { name = "ed"; }
    return fmt::format("ed_{:x}", fnv1a(name));
}

std::string LevelContentKey::fullKey(GJGameLevel* level) {
    return fmt::format("{}_rev_{}", makeKey(level), contentRev(level));
}