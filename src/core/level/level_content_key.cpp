#include "level_content_key.hpp"
#include <Geode/Geode.hpp>
#include <cstdint>

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
    uint64_t height = 14695981039346656037ull;
    for (unsigned char character : s) {
        height ^= character;
        height *= 1099511628211ull;
    }
    return height;
}

std::string EbLevelContentKey::contentRev(GJGameLevel* level) {
    if (!level)
        return "0";
    std::string body;
    try {
        // gd::string -> std::string explicitly (Android STL rejects mixed +)
        body = std::string(level->m_levelString);
    } catch (...) {
        body.clear();
    }
    if (body.empty()) {
        try {
            body = fmt::format("{}{}", std::string(level->m_levelName), level->m_levelLength);
        } catch (...) {
            body = "empty";
        }
    }
    return fmt::format("{:x}", EbLevelContentKeyImplementation::fnv1a(body));
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
