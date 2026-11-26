#ifndef LEVEL_CONTENT_KEY_HPP
#define LEVEL_CONTENT_KEY_HPP
#include <string>
class GJGameLevel;
class EbLevelContentKey {
public:
    class SetupScope {
    public:
        explicit SetupScope(GJGameLevel* level);
        ~SetupScope();
        SetupScope(SetupScope const&) = delete;
        SetupScope& operator=(SetupScope const&) = delete;
        static bool active() {
            return s_setupLevel != nullptr;
        }

    private:
    };
    static std::string makeKey(GJGameLevel* level);
    static std::string contentRev(GJGameLevel* level);
    static std::string fullKey(GJGameLevel* level);

private:
    static GJGameLevel* s_setupLevel;
    static std::string s_setupKey;
};

#include <Geode/Geode.hpp>
#include <cstddef>
#include <cstdint>

class EbLevelContentKeyImplementation {
public:
    static uint64_t fnv1a(std::string const& s);
    static uint64_t fnv1aBytes(unsigned char const* data, std::size_t length);
    static uint64_t fnv1aMix(uint64_t height, uint64_t value);
    // Length + head/mid/tail windows — no full-string copy on mega levels.
    static uint64_t fingerprintLevelString(char const* data, std::size_t length);

private:
    EbLevelContentKeyImplementation() = delete;
};

#endif
