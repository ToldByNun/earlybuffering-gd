#include "test_cube_rescue.hpp"
#include "../src/core/assist/contracts/live_plan_contracts.hpp"
#include <iostream>

int CubeRescueRegression::run() {
    int failures = 0;
    // 013422, press 84: d0 dies at 5, d1..5 die, d6..9 reach 96.
    // The first probe must reach d6 before spending time on the dead prefix.
    failures += EbLivePlanContracts::cubeRescueProbe(12, 0) != 6;
    failures += EbLivePlanContracts::cubeRescueProbe(12, 1) != 9;
    failures += EbLivePlanContracts::cubeRescueProbe(12, 2) != 12;
    failures += EbLivePlanContracts::cubeRescueProbe(0, 0) != -1;
    failures += EbLivePlanContracts::cubeRescueProbe(12, -1) != -1;
    failures += EbLivePlanContracts::cubeRescueProbe(12, 3) != -1;
    for (int limit = 2; limit <= 96; ++limit) {
        int previous = 0;
        for (int index = 0; index < 3; ++index) {
            int const probe = EbLivePlanContracts::cubeRescueProbe(limit, index);
            failures += probe < 1 || probe > limit || probe < previous;
            previous = probe;
        }
    }
    std::cout << "Cube rescue regression: " << failures << " failures" << '\n';
    return failures ? 1 : 0;
}

int main() {
    return CubeRescueRegression::run();
}
