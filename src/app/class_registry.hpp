#ifndef CLASS_REGISTRY_HPP
#define CLASS_REGISTRY_HPP

#include <cstddef>
#include <span>
#include <string_view>

class EbClassInfo {
public:
    std::string_view id;
    std::string_view name;
    std::string_view source;
    std::size_t line;
    bool managed;
    bool hook;

private:
};

class EbClassRegistry {
public:
    static std::span<EbClassInfo const> all();
    static EbClassInfo const* find(std::string_view nameOrId);
    static std::size_t serviceCount();

private:
    EbClassRegistry() = delete;
};

#endif
