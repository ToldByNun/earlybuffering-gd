#ifndef EARLYBUFFER_CORE_LEVELCONTENTKEY_HPP
#define EARLYBUFFER_CORE_LEVELCONTENTKEY_HPP
#include <string>
class GJGameLevel;
class LevelContentKey {
public:
    static std::string makeKey(GJGameLevel* level);
    static std::string contentRev(GJGameLevel* level);
    static std::string fullKey(GJGameLevel* level);
};
#endif