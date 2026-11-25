#include "test_completed_trigger_collection.hpp"
#include "../src/core/trigger_world/completed_trigger_collection.hpp"
#include <iostream>

int CompletedTriggerCollectionRegression::run() {
    int failures = 0;
    int layer = 0;
    int objects = 0;
    int other = 0;
    EbCompletedTriggerCollection cache;
    failures += cache.matches(&layer, &objects, 176, 176, 20, false);
    cache.remember(&layer, &objects, 176, 176, 20, false, false);
    failures += cache.matches(&layer, &objects, 176, 176, 20, false);
    cache.remember(&layer, &objects, 176, 176, 20, false, true);
    // Reproduces thousands of physics ticks with the same fully collected,
    // legitimately empty world. None should start another bake or write a log.
    for (int tick = 0; tick < 10000; ++tick)
        failures += !cache.matches(&layer, &objects, 176, 176, 20, false);
    failures += cache.matches(&other, &objects, 176, 176, 20, false);
    failures += cache.matches(&layer, &other, 176, 176, 20, false);
    failures += cache.matches(&layer, &objects, 177, 176, 20, false);
    failures += cache.matches(&layer, &objects, 176, 177, 20, false);
    failures += cache.matches(&layer, &objects, 176, 176, 21, false);
    failures += cache.matches(&layer, &objects, 176, 176, 20, true);
    cache.clear();
    failures += cache.matches(&layer, &objects, 176, 176, 20, false);
    // Init-race / cancelled collections must always remain retryable.
    cache.remember(&layer, &objects, 0, 0, 0, false, true);
    failures += cache.matches(&layer, &objects, 0, 0, 0, false);
    cache.remember(nullptr, &objects, 176, 176, 20, false, true);
    failures += cache.matches(nullptr, &objects, 176, 176, 20, false);
    // GD 2.2091: 21,378 owned objects, but only 164 visual children. A fully
    // visited empty world must stay cached instead of starting 20-30 bakes/s.
    bool const complete = EbCompletedTriggerCollection::isComplete(21378, 21378, 21378, false);
    failures += !complete;
    cache.remember(&layer, &objects, 21378, 164, 20, false, complete);
    for (int tick = 0; tick < 10000; ++tick)
        failures += !cache.matches(&layer, &objects, 21378, 164, 20, false);
    // Unvisited objects, a changed source, and cancelled collections stay retryable.
    failures += EbCompletedTriggerCollection::isComplete(21378, 164, 21378, false);
    failures += EbCompletedTriggerCollection::isComplete(21378, 21377, 21378, false);
    failures += EbCompletedTriggerCollection::isComplete(21378, 21378, 21379, false);
    failures += EbCompletedTriggerCollection::isComplete(21378, 21378, 21378, true);
    failures += EbCompletedTriggerCollection::isComplete(0, 0, 0, false);
    std::cout << "Completed trigger collection regression: " << failures << " failures\n";
    return failures ? 1 : 0;
}
int main() {
    return CompletedTriggerCollectionRegression::run();
}
