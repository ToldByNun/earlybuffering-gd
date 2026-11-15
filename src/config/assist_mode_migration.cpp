#include "assist_mode_migration.hpp"

char const* EbAssistModeMigration::canonical(std::string const& stored) {
    if (stored == "Hybrid")
        return "Hybrid";
    return "Live";
}
