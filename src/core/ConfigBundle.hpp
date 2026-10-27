#ifndef FRIRECRACKER_CORE_CONFIGBUNDLE_HPP
#define FRIRECRACKER_CORE_CONFIGBUNDLE_HPP
#include <string>
#include <Geode/Geode.hpp>
class ConfigBundle {
public:
    static geode::Result<> exportTo(std::filesystem::path const& path);
    static geode::Result<> importFrom(std::filesystem::path const& path);
};
#endif