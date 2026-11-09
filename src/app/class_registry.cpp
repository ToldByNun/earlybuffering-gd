#include "class_registry.hpp"
#include "eb_class_catalog.hpp"

std::span<EbClassInfo const> EbClassRegistry::all() {
    return EB_CLASS_INFOS;
}

EbClassInfo const* EbClassRegistry::find(std::string_view nameOrId) {
    for (auto const& entry : all()) {
        if (entry.name == nameOrId || entry.id == nameOrId)
            return &entry;
    }
    return nullptr;
}

std::size_t EbClassRegistry::serviceCount() {
    return EB_MANAGED_SERVICE_COUNT;
}
