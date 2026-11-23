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
    // research_20261123_234922: all delays die at the next obstacle around
    // tick 78 although the user makes the next flip around tick 50. A physically
    // completed flip must not demand survival without that future input.
    expect(EbLiveRescuePolicy::classify(false, false, false, 32, 104, true) == safe,
           "completed ball flip is a decision boundary");
    expect(EbLiveRescuePolicy::classify(false, false, false, 32, 104, false) == shallow,
           "short uncompleted flight remains incomplete");
    expect(EbLiveRescuePolicy::classify(true, false, false, 32, 104, true) == dead,
           "landing never overrides collision");
    expect(EbLiveRescuePolicy::classify(false, true, false, 32, 104, true) == shallow,
           "landing never overrides unknown state");
    std::array const ballRescue{dead,
                                EbLiveRescuePolicy::classify(false, false, false, 32, 104, true)};
    expect(EbLiveRescuePolicy::choose(ballRescue) == 1,
           "delay rescues lethal flight with completed flip");
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
    // Nova's safe-band choice: prefer a physically checked interior over a
    // fragile first saving tick, while keeping isolated saves if no band exists.
    for (int mask = 0; mask < (1 << 10); ++mask) {
        EbLiveRescueSearch bandSearch(10, true);
        std::vector<int> seen(11, 0);
        for (int delay = bandSearch.nextDelay(); delay >= 0; delay = bandSearch.nextDelay()) {
            expect(++seen[delay] == 1, "safe-band probes are memoized");
            bool const survives = delay > 0 && (mask & (1 << (delay - 1)));
            bandSearch.record(delay, survives ? safe : dead);
        }
        int const selected = bandSearch.selectedDelay();
        expect(mask == 0 ? selected == -1 : selected > 0 && (mask & (1 << (selected - 1))),
               "safe-band selection never invents a survivor");
        auto const band = bandSearch.safeBand();
        for (std::size_t i = 1; i < band.size(); ++i)
            expect(band[i] == band[i - 1] + 1, "safe band cannot bridge holes");
    }
    EbLiveRescueSearch centered(8, true);
    for (int delay = centered.nextDelay(); delay >= 0; delay = centered.nextDelay())
        centered.record(delay, delay >= 2 && delay <= 6 ? safe : dead);
    expect(centered.selectedDelay() == 4, "choose tested interior of broad saving band");
    EbLiveRescueSearch sturdier(8, true);
    for (int delay = sturdier.nextDelay(); delay >= 0; delay = sturdier.nextDelay())
        sturdier.record(delay, delay == 1 || (delay >= 3 && delay <= 5) ? safe : dead);
    expect(sturdier.selectedDelay() == 4, "prefer sturdy band over isolated earlier save");
    EbLiveRescueSearch underPressure(8, true);
    underPressure.record(0, dead);
    underPressure.record(1, safe);
    underPressure.reserveFallback();
    expect(underPressure.nextDelay() == -1 && underPressure.selectedDelay() == 1,
           "budget pressure keeps proved save without band expansion");
    EbLiveRescueSearch truncatedBand(8, true);
    truncatedBand.record(0, dead);
    truncatedBand.record(1, safe);
    truncatedBand.record(2, shallow);
    expect(truncatedBand.selectedDelay() == 1, "unknown neighbor never moves the chosen tick");
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
