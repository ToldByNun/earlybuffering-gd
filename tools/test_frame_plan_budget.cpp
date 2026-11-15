#include "test_frame_plan_budget.hpp"
#include "../src/core/assist/contracts/frame_plan_budget.hpp"
#include <iostream>

int EbFramePlanBudgetTests::run() {
    int failures = 0;
    auto expect = [&](bool result, char const* label) {
        if (!result) {
            ++failures;
            std::cerr << label << std::endl;
        }
    };
    EbFramePlanBudget budget;
    budget.begin(10, 1000);
    expect(budget.remaining(1000, 1100, 100) == 800, "small explicit budget and exit reserve");
    budget.end(1400);
    budget.begin(10, 4000);
    expect(budget.remaining(1000, 4100, 100) == 400,
           "release shares press cost, excludes idle gap");
    budget.end(4300);
    expect(budget.spent(9000) == 700, "completed scopes accumulate");
    budget.begin(10, 10000);
    expect(budget.remaining(500, 10000, 100) == 0, "exhaustion never becomes unlimited");
    expect(budget.remaining(0, 10000) == 0, "zero allowance");
    expect(budget.remaining(-1, 10000) == 0, "invalid allowance");
    budget.end(10100);
    budget.begin(11, 20000);
    expect(budget.remaining(1000, 20000, 100) == 900, "next frame resets");
    budget.begin(11, 20100);
    budget.end(20200);
    budget.end(20400);
    expect(budget.spent(30000) == 400, "nested planning charged once");
    budget.begin(12, 40000);
    budget.end(39999);
    expect(budget.spent(40000) == 0, "negative clock delta cannot refund work");
    budget.end(50000);
    expect(budget.spent(50000) == 0, "unmatched end is harmless");
    std::cout << "Frame planning budget: " << failures << " failures" << std::endl;
    return failures == 0 ? 0 : 1;
}

int main() {
    return EbFramePlanBudgetTests::run();
}
