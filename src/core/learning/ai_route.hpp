#ifndef AI_ROUTE_HPP
#define AI_ROUTE_HPP

#include <cstdint>
#include <vector>

// Lightweight route types shared by Autoplay + RoutePlanner (no Geode / Shadow deps).
class EbAiRouteEvent {
public:
    enum class Kind : std::uint8_t { Press = 0, Release = 1 };

    Kind kind = Kind::Press;
    int delay = 0;     // ticks from route start (relative to plan tick)
    int holdTicks = 0; // for Press: planned hold before Release
    float neuralScore = 0.f;
    bool verified = false;
    bool clearsThreat = false;

private:
};

class EbAiRoute {
public:
    std::vector<EbAiRouteEvent> events;
    int planTick = -1;
    float startX = 0.f;
    float routeScore = 0.f;
    int simCalls = 0;
    int neuralEvals = 0;
    bool budgetHit = false;
    bool valid = false;
    char const* reason = "empty";

    void clear();

private:
};

#endif
