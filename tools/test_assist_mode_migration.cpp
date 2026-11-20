#include "test_assist_mode_migration.hpp"
#include "../src/config/assist_mode_migration.hpp"
#include <iostream>
#include <string>

int AssistModeMigrationTests::run() {
    int failures = 0;
    for (std::string const value :
         {"Live", "Hybrid", "AI", "Replay", "BruteForce", "Route", "", "unknown"}) {
        std::string expected = "Live";
        if (value == "Hybrid")
            expected = "Hybrid";
        else if (value == "AI")
            expected = "AI";
        std::string const migrated = EbAssistModeMigration::canonical(value);
        failures += migrated != expected;
        failures += migrated != EbAssistModeMigration::canonical(migrated);
    }
    std::cout << "Assist mode migration: " << failures << " failures" << std::endl;
    return failures ? 1 : 0;
}

int main() {
    return AssistModeMigrationTests::run();
}
