#include "test_live_rescue_policy.hpp"
#include "../src/core/assist/contracts/live_horizon_controller.hpp"
#include "../src/core/assist/contracts/live_rescue_policy.hpp"
#include "../src/core/assist/contracts/live_rescue_search.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

int LiveRescueRegression::run() {
    int failures = 0;
    auto expect = [&](bool passed, char const* name) {
        if (passed)
            return;
        ++failures;
        std::cerr << name << '\n';
    };
    // Captured outcome pattern: immediate input collides at tick 8, later input
    // is still alive at 24 but collides at 40. A 96-tick observation cannot call
    // the 24-tick screen safe, even after the original obstacle has been cleared.
    auto const dead = EbLiveRescuePolicy::classify(true, false, false, 9, 96);
    auto const shallow = EbLiveRescuePolicy::classify(false, false, false, 24, 96);
    auto const laterDead = EbLiveRescuePolicy::classify(true, false, false, 41, 96);
    auto const safe = EbLiveRescuePolicy::classify(false, false, false, 96, 96);
    std::array const falseSave{dead, shallow, laterDead};
    expect(EbLiveRescuePolicy::choose(falseSave) == -1, "short survivor is no rescue");
    std::array const rescue{dead, shallow, laterDead, safe};
    expect(EbLiveRescuePolicy::choose(rescue) == 3, "skip incomplete and lethal alternatives");
    std::array const originalSafe{safe, safe, safe, safe};
    expect(EbLiveRescuePolicy::choose(originalSafe) == 0, "preserve safe original input");
    std::array const originalUnknown{shallow, safe, safe};
    expect(EbLiveRescuePolicy::choose(originalUnknown) == -1,
           "no delay without known baseline death");
    std::array const earliest{dead, safe, safe, safe};
    expect(EbLiveRescuePolicy::choose(earliest) == 1, "earliest fully proved survivor wins");
    expect(EbLiveRescuePolicy::choose({}) == -1, "empty observations");
    // A death exactly on the shared observation boundary is still lethal.
    expect(EbLiveRescuePolicy::classify(true, false, false, 96, 96) == dead,
           "boundary collision is rejected");
    expect(EbLiveRescuePolicy::classify(false, true, false, 96, 96) == shallow,
           "full-length unknown is not proof");
    // Budget truncation, early landing, and a short adaptive seed are all just
    // partial observations; none changes the shared target horizon.
    for (int horizon = 1; horizon <= 320; ++horizon) {
        for (int ticks = 0; ticks <= horizon; ++ticks) {
            for (int flags = 0; flags < 8; ++flags) {
                auto const observed =
                    EbLiveRescuePolicy::classify(flags & 1, flags & 2, flags & 4, ticks, horizon);
                bool const proved = observed == EbLiveRescuePolicy::Observation::Safe;
                if (proved != (flags == 0 && ticks == horizon))
                    ++failures;
            }
        }
    }
    // Exhaust all small safe-window shapes, including disconnected and
    // one-tick windows. Coarse sampling may reorder work, never exclude proof.
    for (int limit = 1; limit <= 12; ++limit) {
        for (unsigned mask = 0; mask < (1u << limit); ++mask) {
            EbLiveRescueSearch search(limit);
            std::vector<int> tested(static_cast<std::size_t>(limit) + 1, 0);
            int count = 0;
            for (int delay = search.nextDelay(); delay >= 0; delay = search.nextDelay()) {
                if (++count > limit + 1 || ++tested[static_cast<std::size_t>(delay)] > 1) {
                    ++failures;
                    break;
                }
                search.record(delay, delay > 0 && (mask & (1u << (delay - 1)))
                                         ? EbLiveRescuePolicy::Observation::Safe
                                         : EbLiveRescuePolicy::Observation::Dead);
            }
            int const selected = search.selectedDelay();
            if ((mask == 0 && selected != -1) ||
                (mask != 0 && (selected <= 0 || !(mask & (1u << (selected - 1))))))
                ++failures;
        }
    }
    for (int limit = 1; limit <= 96; ++limit) {
        for (int survivor = 1; survivor <= limit; ++survivor) {
            EbLiveRescueSearch search(limit);
            for (int delay = search.nextDelay(); delay >= 0; delay = search.nextDelay()) {
                search.record(delay, delay == survivor ? EbLiveRescuePolicy::Observation::Safe
                                                       : EbLiveRescuePolicy::Observation::Dead);
            }
            if (search.selectedDelay() != survivor)
                ++failures;
        }
    }
    // Exhaust disjoint narrow/broad windows, including holes: coarse misses
    // carry no implication about the skipped ticks, and no nominal tick repeats.
    for (int limit = 1; limit <= 12; ++limit) {
        for (int mask = 0; mask < (1 << limit); ++mask) {
            EbLiveRescueSearch grid(limit);
            std::vector<int> calls(static_cast<std::size_t>(limit) + 1, 0);
            for (int delay = grid.nextDelay(); delay >= 0; delay = grid.nextDelay()) {
                if (++calls[static_cast<std::size_t>(delay)] > 1) {
                    ++failures;
                    break;
                }
                bool const safeTick = delay > 0 && (mask & (1 << (delay - 1)));
                grid.record(delay, safeTick ? EbLiveRescuePolicy::Observation::Safe
                                            : EbLiveRescuePolicy::Observation::Dead);
            }
            int const chosen = grid.selectedDelay();
            if ((mask == 0 && chosen != -1) ||
                (mask != 0 && (chosen <= 0 || !(mask & (1 << (chosen - 1))))))
                ++failures;
        }
    }
    EbLiveRescueSearch interrupted(32);
    interrupted.record(0, EbLiveRescuePolicy::Observation::Dead);
    interrupted.record(1, EbLiveRescuePolicy::Observation::Dead);
    interrupted.record(5, EbLiveRescuePolicy::Observation::Incomplete);
    interrupted.record(9, EbLiveRescuePolicy::Observation::Safe);
    expect(interrupted.selectedDelay() == 9,
           "keep proved survivor when refinement runs out of time");
    interrupted.record(6, EbLiveRescuePolicy::Observation::Incomplete);
    expect(interrupted.selectedDelay() == 9, "partial refinement cannot supersede proof");
    EbLiveRescueSearch cached(48);
    cached.record(0, EbLiveRescuePolicy::Observation::Dead);
    cached.record(1, EbLiveRescuePolicy::Observation::Safe);
    cached.record(1, EbLiveRescuePolicy::Observation::Incomplete);
    expect(cached.selectedDelay() == 1, "same-press cached proof is immutable");
    // Compare actual production scheduling on a broad synthetic safe window.
    // These are probe counts, not FPS or measured game-time claims.
    EbLiveRescueSearch broad(48);
    int broadProbes = 0;
    int independentIdle = 0;
    int sharedIdle = 0;
    int previousDelay = 0;
    for (int delay = broad.nextDelay(); delay >= 0; delay = broad.nextDelay()) {
        ++broadProbes;
        if (delay > 0) {
            independentIdle += delay;
            sharedIdle += delay < previousDelay ? delay : delay - previousDelay;
            previousDelay = delay;
        }
        broad.record(delay, delay >= 25 ? EbLiveRescuePolicy::Observation::Safe
                                        : EbLiveRescuePolicy::Observation::Dead);
    }
    expect(broad.selectedDelay() == 25 && broadProbes < 26,
           "coarse search reduces broad-window probes");
    expect(sharedIdle < independentIdle, "ordered prefix restarts still save idle work");
    EbLiveHorizonController horizonControl;
    expect(horizonControl.scale(0, true, true) == 1.f, "initial horizon");
    horizonControl.observe(0, true, true, true, 4000, 4000);
    expect(std::abs(horizonControl.scale(0, true, true) - .85f) < .0001f, "Nova timeout reduction");
    expect(horizonControl.scale(0, false, true) == 1.f &&
               horizonControl.scale(0, true, false) == 1.f &&
               horizonControl.scale(1, true, true) == 1.f,
           "isolate player edge and mode feedback");
    for (int index = 0; index < 20; ++index)
        horizonControl.observe(0, true, true, true, 4000, 4000);
    expect(horizonControl.scale(0, true, true) == .5f, "half-horizon floor");
    for (int index = 0; index < 40; ++index)
        horizonControl.observe(0, true, true, false, 500, 4000);
    expect(horizonControl.scale(0, true, true) == 1.f, "gradual recovery stops at full horizon");
    expect(EbLiveHorizonController::scaledCap(96, .5f) == 48, "scaled shared target");
    expect(EbLiveHorizonController::scaledCap(96, std::numeric_limits<float>::quiet_NaN()) == 96,
           "invalid scale cannot shorten proof");
    horizonControl.reset();
    expect(horizonControl.scale(0, true, true) == 1.f, "new level resets feedback");
    std::cout << "Broad-window probes " << broadProbes << " vs 26 linear; idle steps " << sharedIdle
              << " vs " << independentIdle << " independent\n";
    std::cout << "Live rescue regression: " << failures << " failures\n";
    return failures ? 1 : 0;
}

int main() {
    return LiveRescueRegression::run();
}
