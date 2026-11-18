#include "test_ship_line_policy.hpp"
#include "../src/core/assist/flight/ship_line_policy.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
int ShipLineRegression::run() {
    int failures = 0;
    int cases = 0;
    for (double angle : {-45., -20., 0., 20., 45.}) {
        for (bool reverse : {false, true}) {
            for (bool sideways : {false, true}) {
                for (double size : {.6, 1.}) {
                    EbShipLinePolicy::Target target;
                    target.enabled = true;
                    target.x = 20000.;
                    target.y = 300.;
                    target.degrees = angle;
                    target.bodySize = 30. * size;
                    target.reversed = reverse;
                    target.sideways = sideways;
                    EbShipLinePolicy::Metrics original;
                    EbShipLinePolicy::Metrics corrected;
                    for (int tick = 1; tick <= 32; ++tick) {
                        for (int branch = 0; branch < 2; ++branch) {
                            double const heading =
                                (angle + (branch == 0 ? 20. : 2.)) * std::numbers::pi / 180.;
                            double const forward =
                                (reverse ? -1. : 1.) * std::cos(heading) * 4. * size;
                            double const transverse = std::sin(heading) * 4. * size;
                            double const dx = sideways ? transverse : forward;
                            double const dy = sideways ? forward : transverse;
                            EbShipLinePolicy::observe(target, branch == 0 ? original : corrected,
                                                      target.x + dx * tick, target.y + dy * tick,
                                                      dx, dy);
                        }
                    }
                    failures += !EbShipLinePolicy::improves(original, corrected, 2, 8);
                    failures += EbShipLinePolicy::improves(corrected, corrected, 2, 8);
                    auto excessiveTurn = corrected;
                    excessiveTurn.lastHeadingError = original.lastHeadingError + 21.;
                    failures += EbShipLinePolicy::improves(original, excessiveTurn, 2, 8);
                    failures += EbShipLinePolicy::improves(corrected, original, 2, 8);
                    failures += EbShipLinePolicy::improves(original, corrected, 9, 8);
                    failures += EbShipLinePolicy::improves(original, corrected, 0, 8);
                    auto incomplete = corrected;
                    --incomplete.samples;
                    failures += EbShipLinePolicy::improves(original, incomplete, 2, 8);
                    incomplete = corrected;
                    incomplete.invalid = true;
                    failures += EbShipLinePolicy::improves(original, incomplete, 2, 8);
                    ++cases;
                }
            }
        }
    }
    EbShipLinePolicy::Metrics empty;
    failures += std::isfinite(EbShipLinePolicy::cost(empty, 0, 8));
    EbShipLinePolicy::Target target;
    target.enabled = true;
    target.bodySize = 0.;
    EbShipLinePolicy::observe(target, empty, 0., 0., 1., 0.);
    failures += !empty.invalid;
    std::cout << cases << " line/direction/size cases, " << failures << " failures\n";
    return failures ? 1 : 0;
}
int main() {
    return ShipLineRegression::run();
}
