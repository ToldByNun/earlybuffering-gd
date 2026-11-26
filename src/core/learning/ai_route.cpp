#include "ai_route.hpp"

void EbAiRoute::clear() {
    events.clear();
    planTick = -1;
    startX = 0.f;
    routeScore = 0.f;
    simCalls = 0;
    neuralEvals = 0;
    budgetHit = false;
    valid = false;
    reason = "empty";
}
