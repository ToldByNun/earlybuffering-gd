#include "test_config_keys.hpp"
#include "../src/core/bundle/config_bundle_keys.hpp"
#include <iostream>
#include <string>

int ConfigKeysRegression::run() {
    std::string report;
    bool const valid = EbConfigBundleKeys::selfCheck(report);
    std::cout << report << std::endl;
    return valid ? 0 : 1;
}

int main() {
    return ConfigKeysRegression::run();
}
