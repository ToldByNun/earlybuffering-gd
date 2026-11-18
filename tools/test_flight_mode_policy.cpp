#include "test_flight_mode_policy.hpp"
#include "../src/core/assist/flight/flight_mode_policy.hpp"
#include <iostream>
#include <string>

int FlightModePolicyRegression::run() {
    std::string report;
    int failures = EbFlightModePolicy::selfCheck(report) ? 0 : 1;
    if (failures)
        std::cout << "flight mode policy failures: " << report << "\n";
    else
        std::cout << "flight mode policy ok\n";
    return failures ? 1 : 0;
}

int main() {
    return FlightModePolicyRegression::run();
}
