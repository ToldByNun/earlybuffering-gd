#include "test_spider_continuation.hpp"
#include "../src/core/sim/planning/spider_continuation.hpp"
#include <iostream>

int SpiderContinuationRegression::run() {
    int failures = 0;
    // Recorded 122956: d0 dies at 4, d3/d4 last until 66.
    failures += !EbSpiderContinuation::allowed(4, 3, 66, 50);
    // Recorded 122759: d0 dies at 5, d4 lasts until 67.
    failures += !EbSpiderContinuation::allowed(5, 4, 67, 51);
    failures += EbSpiderContinuation::probe(12, 0) != 3;
    failures += EbSpiderContinuation::probe(12, 1) != 4;
    failures += EbSpiderContinuation::probe(12, 2) != 6;
    failures += EbSpiderContinuation::probe(12, 3) != 12;
    failures += EbSpiderContinuation::probe(0, 0) != -1;
    failures += EbSpiderContinuation::allowed(4, 3, 5, 4);
    failures += EbSpiderContinuation::allowed(-1, 3, 66, 50);
    failures += EbSpiderContinuation::allowed(4, 3, 66, 4);
    failures += EbSpiderContinuation::allowed(4, 3, 66, 66);
    // A gain alone, rejected second click, death, unknown state or shallow run
    // must never qualify. These are proof gates, not a native physics replay.
    failures += !EbSpiderContinuation::verified(false, false, 96, 96, true, 50);
    failures += EbSpiderContinuation::verified(true, false, 96, 96, true, 50);
    failures += EbSpiderContinuation::verified(false, true, 96, 96, true, 50);
    failures += EbSpiderContinuation::verified(false, false, 95, 96, true, 50);
    failures += EbSpiderContinuation::verified(false, false, 96, 96, false, 50);
    failures += EbSpiderContinuation::verified(false, false, 96, 96, true, 73);
    std::cout << "Spider continuation regression: " << failures << " failures\n";
    return failures ? 1 : 0;
}
int main() {
    return SpiderContinuationRegression::run();
}
