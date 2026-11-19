#include "test_input_reconciler.hpp"
#include "../src/core/assist/input/input_reconciler.hpp"
#include <iostream>
int InputReconcilerRegression::run() {
    int failures = 0;
    for (int player = 0; player < 2; ++player) {
        for (int index = 0; index < 64; ++index) {
            EbInputReconciler reconciler;
            EbInputReconciler::Source a;
            a.device = EbInputReconciler::Device::Keyboard;
            a.id = index;
            auto b = a;
            b.id = index + 100;
            failures += !reconciler.request(a, player, true, 0);
            failures += reconciler.state(player).dispatched;
            failures += reconciler.request(a, player, true, 1);
            failures += reconciler.request(b, player, true, 2);
            failures += reconciler.request(a, player, false, 3);
            failures += !reconciler.state(player).requested;
            reconciler.acknowledge(player, true, 4);
            failures += !reconciler.request(b, player, false, 5);
            failures += !reconciler.state(player).dispatched;
            failures += reconciler.state(1 - player).requested;
            reconciler.acknowledge(player, false, 6);
            failures += reconciler.state(player).dispatched;
            reconciler.reset();
            failures += reconciler.state(player).physical;
        }
    }
    std::cout << "128 ownership sequences, " << failures << " failures\n";
    return failures ? 1 : 0;
}
int main() {
    return InputReconcilerRegression::run();
}
