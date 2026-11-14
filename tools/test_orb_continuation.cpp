#include "test_orb_continuation.hpp"
#include "../src/core/sim/planning/orb_continuation.hpp"
#include <iostream>

int OrbContinuationRegression::run() {
    int failures = 0;
    EbOrbContinuation chain;
    failures += chain.next(1, 0, true) != EbOrbContinuation::Input::None;
    failures += chain.next(5, 1, false) != EbOrbContinuation::Input::Release;
    failures += chain.next(5, 1, true) != EbOrbContinuation::Input::None;
    failures += chain.next(6, 1, false) != EbOrbContinuation::Input::None;
    failures += chain.next(20, 1, true) != EbOrbContinuation::Input::Press;
    failures += chain.pressTick() != 20;
    failures += chain.next(21, 2, false) != EbOrbContinuation::Input::Release;
    failures += chain.next(22, 2, true) != EbOrbContinuation::Input::None;
    failures += chain.next(23, 3, true) != EbOrbContinuation::Input::None;
    // Mere contact or a rejected press cannot become a continuation proof.
    failures += chain.verified(1, 96);
    failures += chain.verified(2, 43);
    failures += !chain.verified(2, 44);
    EbOrbContinuation noFollowup;
    failures += noFollowup.next(5, 1, false) != EbOrbContinuation::Input::Release;
    failures += noFollowup.attempted();
    failures += noFollowup.verified(2, 96);
    std::cout << "Orb continuation regression: " << failures << " failures\n";
    return failures ? 1 : 0;
}
int main() {
    return OrbContinuationRegression::run();
}
