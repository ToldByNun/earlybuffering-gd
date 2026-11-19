#include "test_selected_tick_guard.hpp"
#include "../src/core/assist/contracts/selected_tick_guard.hpp"
#include <array>
#include <iostream>

int SelectedTickRegression::run() {
    int failures = 0;
    failures += EbSelectedTickGuard::classifyProbe(true, false, false, 100, 96, true) !=
                EbSelectedTickGuard::ProbeState::Rejected;
    failures += EbSelectedTickGuard::classifyProbe(false, true, false, 100, 96, true) !=
                EbSelectedTickGuard::ProbeState::Incomplete;
    failures += EbSelectedTickGuard::classifyProbe(false, false, false, 24, 96, true) !=
                EbSelectedTickGuard::ProbeState::Incomplete;
    failures += EbSelectedTickGuard::classifyProbe(false, false, true, 12, 96, true) !=
                EbSelectedTickGuard::ProbeState::Verified;
    failures += EbSelectedTickGuard::classifyProbe(false, false, false, 96, 96, true) !=
                EbSelectedTickGuard::ProbeState::Verified;
    failures += EbSelectedTickGuard::classifyProbe(false, false, false, 96, 96, false) !=
                EbSelectedTickGuard::ProbeState::Rejected;

    std::array const band{3, 4, 5};
    failures += EbSelectedTickGuard::choose(3, 8, 1, band) != 3;
    failures += EbSelectedTickGuard::choose(4, 8, 1, band) != 4;
    failures += EbSelectedTickGuard::choose(0, 8, 1, band) != 0;
    std::array const cubeFirst{3, 4};
    failures += EbSelectedTickGuard::choose(3, 12, 3, cubeFirst) != 3;
    std::array const cubeLate{10, 11, 12};
    failures += EbSelectedTickGuard::choose(10, 12, 3, cubeLate) != 10;
    std::array const isolated{3};
    failures += EbSelectedTickGuard::choose(3, 8, 1, isolated) != 3;
    std::array const gaps{3, 5, 6};
    failures += EbSelectedTickGuard::hasMargin(5, gaps);
    failures += EbSelectedTickGuard::choose(3, 8, 1, gaps) != 3;
    std::array const alternative{5, 6, 7};
    failures += EbSelectedTickGuard::choose(3, 8, 3, alternative) != 6;
    failures += EbSelectedTickGuard::choose(3, 8, 1, alternative) != -1;
    failures += EbSelectedTickGuard::keepVerifiedLearningDelay(6, 3, band) != 3;
    failures += EbSelectedTickGuard::keepVerifiedLearningDelay(4, 3, band) != 4;
    failures += EbSelectedTickGuard::keepVerifiedLearningDelay(3, 3, {}) != 3;
    std::array const windowEdge{6, 7, 8};
    failures += EbSelectedTickGuard::choose(8, 8, 1, windowEdge) != 8;
    // Every already-verified Cube tick must remain invariant under the guard,
    // regardless of the available margin or the configured robust radius.
    for (int limit = 2; limit <= 96; ++limit) {
        for (int selected = 1; selected <= limit; ++selected) {
            std::array const nominal{selected};
            failures += EbSelectedTickGuard::choose(selected, limit, 3, nominal) != selected;
        }
    }
    // A margin must fit inside the current configured window even if a
    // caller supplies proof ticks collected under a larger prior window.
    for (int limit = 3; limit <= 96; ++limit) {
        std::array const staleBand{limit - 1, limit, limit + 1};
        failures += EbSelectedTickGuard::choose(limit - 2, limit, 3, staleBand) != -1;
    }
    // Exhaust the truth flags at both sides of the observation boundary.
    for (int flags = 0; flags < 16; ++flags) {
        for (int ticks : {0, 1, 23, 24, 25}) {
            bool const died = flags & 1;
            bool const unknown = flags & 2;
            bool const landed = flags & 4;
            bool const clear = flags & 8;
            auto const result =
                EbSelectedTickGuard::classifyProbe(died, unknown, landed, ticks, 24, clear);
            bool const verified = result == EbSelectedTickGuard::ProbeState::Verified;
            failures +=
                verified != (!died && !unknown && ticks > 0 && (landed || ticks >= 24) && clear);
        }
    }
    std::cout << "Selected tick regression: " << failures << " failures" << '\n';
    return failures ? 1 : 0;
}

int main() {
    return SelectedTickRegression::run();
}
