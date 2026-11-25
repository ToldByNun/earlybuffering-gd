#include "test_simulation_window.hpp"
#include "../src/core/sim/world/simulation_window.hpp"
#include <iostream>
#include <limits>

int SimulationWindowRegression::run() {
    int failures = 0;
    EbSimulationWindow window(1000.f, 120.f, 40.f);
    failures += !window.overlaps(550.f, 30.f);
    failures += !window.overlaps(1450.f, 30.f);
    failures += window.overlaps(1451.f, 30.f);
    failures += window.overlaps(519.f, 30.f);
    // A huge object whose center is outside still overlaps the local strip.
    failures += !window.overlaps(400.f, 300.f);
    // A mover can sweep through from either side between stored samples.
    failures += !window.overlaps(0.f, 2000.f);
    failures += !window.contains(900.f, 30.f);
    failures += window.contains(1440.f, 30.f);
    failures += window.contains(540.f, 30.f);
    EbSimulationWindow fast(1000.f, 1200.f, 800.f);
    failures += !fast.overlaps(2100.f, 30.f);
    failures += !fast.overlaps(250.f, 30.f);
    float const invalid = std::numeric_limits<float>::quiet_NaN();
    failures += !window.overlaps(invalid, 30.f);
    failures += window.contains(invalid, 30.f);
    failures += !EbSimulationWindow().overlaps(0.f, 30.f);
    failures += EbSimulationWindow().contains(0.f, 30.f);
    std::cout << "Simulation X window (full Y): " << failures << " failures\n";
    return failures ? 1 : 0;
}
int main() {
    return SimulationWindowRegression::run();
}
