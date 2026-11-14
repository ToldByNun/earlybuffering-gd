#include "test_ufo_rescue.hpp"
#include "../src/core/assist/contracts/live_plan_contracts.hpp"
#include <array>
#include <iostream>

int UfoRescueRegression::run() {
    int failures = 0;
    failures += EbLivePlanContracts::repeatedDeathCutAllowed(true, true);
    failures += !EbLivePlanContracts::repeatedDeathCutAllowed(false, true);
    failures += EbLivePlanContracts::repeatedDeathCutAllowed(false, false);
    failures += EbLivePlanContracts::ufoRescueProbe(12, 0) != 3;
    failures += EbLivePlanContracts::ufoRescueProbe(8, 0) != 2;
    failures += EbLivePlanContracts::ufoRescueProbe(1, 0) != -1;
    for (int limit = 2; limit <= 96; ++limit) {
        int previous = 0;
        for (int index = 0; index < 3; ++index) {
            int const probe = EbLivePlanContracts::ufoRescueProbe(limit, index);
            failures += probe <= 0 || probe > limit || probe < previous;
            previous = probe;
        }
    }
    // Synthetic non-monotonic window: equal early ceiling deaths do not
    // establish that the next delayed flap also dies.
    std::array const deaths{10, 10, 10, -1, 12, 12};
    int chosen = -1;
    for (int delay = 0; delay < static_cast<int>(deaths.size()); ++delay) {
        if (delay == 2 && EbLivePlanContracts::repeatedDeathCutAllowed(true, true))
            break;
        if (deaths[delay] < 0) {
            chosen = delay;
            break;
        }
    }
    failures += chosen != 3;
    std::cout << "UFO rescue regression: " << failures << " failures" << '\n';
    return failures ? 1 : 0;
}

int main() {
    return UfoRescueRegression::run();
}
