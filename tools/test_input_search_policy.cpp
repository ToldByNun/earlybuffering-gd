#include "test_input_search_policy.hpp"
#include "../src/core/assist/contracts/input_search_policy.hpp"
#include <iostream>
int InputSearchRegression::run() {
    int failures = 0;
    int cases = 0;
    for (int index = 0; index < 8; ++index) {
        auto const mode = index;
        for (bool down : {false, true}) {
            for (bool dash : {false, true}) {
                bool const releaseMode = index == 1 || index == 4 || index == 5 || index == 7;
                failures += EbInputSearchPolicy::usesInputWindow(mode, down, dash) !=
                            (down || dash || releaseMode);
                for (int limit : {0, 1, 8, 96}) {
                    for (int flags = 0; flags < 8; ++flags) {
                        bool const dead = flags & 1;
                        bool const unknown = flags & 2;
                        bool const beforeInput = flags & 4;
                        bool const rescue =
                            EbInputSearchPolicy::needsRescue(dead, unknown, beforeInput, limit);
                        failures += rescue != (flags == 1 && limit > 0);
                        ++cases;
                    }
                }
            }
        }
    }
    failures += EbInputSearchPolicy::usesInputWindow(8, true, true);
    failures += EbInputSearchPolicy::usesInputWindow(-1, true, true);
    std::cout << cases << " mode/input/rescue combinations, " << failures << " failures\n";
    return failures ? 1 : 0;
}
int main() {
    return InputSearchRegression::run();
}
