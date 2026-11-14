#include "subtick_timing.hpp"
#include <algorithm>
#include <cmath>

bool EbSubtickTiming::withinWindow(int delay, float phase, int limit) {
    return delay >= 1 && std::isfinite(phase) && phase > 0.f && phase < 1.f &&
           static_cast<double>(delay) + phase <= limit;
}

int EbSubtickTiming::ufoBoundaryDelay(int idleDeathTick, int limit) {
    if (idleDeathTick < 1 || limit < 1)
        return -1;
    return std::min(idleDeathTick, limit);
}

float EbSubtickTiming::probePhase(int index) {
    if (index == 0)
        return 0.5f;
    if (index == 1)
        return 0.25f;
    return index == 2 ? 0.75f : -1.f;
}

float EbSubtickTiming::segmentPrefix(float phase, float fullStep, float elapsed, float segment) {
    if (!std::isfinite(phase) || !std::isfinite(fullStep) || !std::isfinite(elapsed) ||
        !std::isfinite(segment) || phase <= 0.f || phase >= 1.f || fullStep <= 0.f ||
        elapsed < 0.f || segment <= 0.f)
        return -1.f;
    float const prefix = phase * fullStep - elapsed;
    if (prefix > segment + fullStep * 0.00001f)
        return -1.f;
    return std::clamp(prefix, 0.f, segment);
}
