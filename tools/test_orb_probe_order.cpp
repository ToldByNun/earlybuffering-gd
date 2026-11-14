#include "test_orb_probe_order.hpp"
#include "../src/core/assist/contracts/orb_probe_order.hpp"
#include <algorithm>
#include <iostream>
#include <vector>

int OrbProbeOrderRegression::run() {
    int failures = 0;
    // A late orb window must be reached before d1..4 exhaust the budget.
    failures += EbOrbProbeOrder::window(9, 12, 12) != std::vector<int>({10, 9, 11, 12});
    failures += EbOrbProbeOrder::window(0, 2, 12) != std::vector<int>({1, 2});
    failures += EbOrbProbeOrder::window(12, 14, 12) != std::vector<int>({12});
    failures += !EbOrbProbeOrder::window(13, 14, 12).empty();
    failures += !EbOrbProbeOrder::window(-1, 4, 12).empty();
    failures += !EbOrbProbeOrder::window(4, 3, 12).empty();
    for (int first = 0; first <= 12; ++first) {
        for (int last = first; last <= 16; ++last) {
            auto order = EbOrbProbeOrder::window(first, last, 12);
            std::sort(order.begin(), order.end());
            std::vector<int> expected;
            for (int delay = std::max(1, first); delay <= std::min(12, last); ++delay)
                expected.push_back(delay);
            failures += order != expected;
        }
    }
    std::cout << "Orb probe order regression: " << failures << " failures\n";
    return failures ? 1 : 0;
}
int main() {
    return OrbProbeOrderRegression::run();
}
