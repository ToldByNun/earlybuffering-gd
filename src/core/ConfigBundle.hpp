#ifndef EARLYBUFFER_CORE_CONFIGBUNDLE_HPP
#define EARLYBUFFER_CORE_CONFIGBUNDLE_HPP

#include <filesystem>
#include <string>
#include <Geode/Geode.hpp>

// Portable settings JSON. v2 = { "version": 2, "settings": { ... } }.
// v1 flat root still accepted on import (Commit 3).
class ConfigBundle {
public:
    static constexpr int kFormatVersion = 2;

    static geode::Result<> exportTo(std::filesystem::path const& path);
    static geode::Result<> importFrom(std::filesystem::path const& path);

private:
    ConfigBundle() = default;
};

#endif // EARLYBUFFER_CORE_CONFIGBUNDLE_HPP
