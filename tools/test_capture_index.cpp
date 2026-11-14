#include "test_capture_index.hpp"
#include "../src/core/sim/world/capture_index_key.hpp"
#include <iostream>

int CaptureIndexRegression::run() {
    int failures = 0;
    int layer = 0;
    int otherLayer = 0;
    int source = 0;
    int otherSource = 0;
    EbCaptureIndexKey key;
    failures += key.matches(&layer, &source, 100000);
    int rebuilds = 0;
    for (int input = 0; input < 1000; ++input) {
        if (!key.matches(&layer, &source, 100000)) {
            ++rebuilds;
            key.bind(&layer, &source, 100000);
        }
    }
    failures += rebuilds != 1;
    failures += key.matches(&otherLayer, &source, 100000);
    failures += key.matches(&layer, &otherSource, 100000);
    failures += key.matches(&layer, &source, 100001);
    failures += key.matches(&layer, &source, 99999);
    key.reset();
    failures += key.matches(&layer, &source, 100000);
    failures += key.matches(nullptr, nullptr, 0);
    std::cout << "Capture index regression: " << failures << " failures" << '\n';
    return failures ? 1 : 0;
}

int main() {
    return CaptureIndexRegression::run();
}
