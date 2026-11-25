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
#include <cstdint>

class EbLevelContentKeyImplementation {
public:
    static uint64_t fnv1a(std::string const& s);

private:
    EbLevelContentKeyImplementation() = delete;
};

#endif
