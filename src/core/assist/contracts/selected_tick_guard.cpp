#include "selected_tick_guard.hpp"
#include <algorithm>

EbSelectedTickGuard::ProbeState EbSelectedTickGuard::classifyProbe(bool died, bool unknown,
                                                                   bool landed, int ticksRan,
                                                                   int observationFloor,
                                                                   bool clearsThreat) {
    if (died)
        return ProbeState::Rejected;
    if (unknown || ticksRan <= 0 || (!landed && ticksRan < observationFloor))
        return ProbeState::Incomplete;
    return clearsThreat ? ProbeState::Verified : ProbeState::Rejected;
}

bool EbSelectedTickGuard::hasMargin(int delay, std::span<int const> verified) {
    return std::ranges::find(verified, delay - 1) != verified.end() &&
           std::ranges::find(verified, delay) != verified.end() &&
           std::ranges::find(verified, delay + 1) != verified.end();
}

int EbSelectedTickGuard::choose(int selected, int limit, int radius,
                                std::span<int const> verified) {
    if (selected <= 0 || selected > limit)
        return selected;
    // A verified winning tick keeps its timing. Neighbour margin only ranks
    // replacements after that tick has actually failed a probe.
    if (std::ranges::find(verified, selected) != verified.end())
        return selected;
    for (int distance = 1; distance <= std::clamp(radius, 1, 3); ++distance) {
        int const later = selected + distance;
        if (later <= limit && hasMargin(later, verified))
            return later;
        int const earlier = selected - distance;
        if (earlier > 0 && hasMargin(earlier, verified))
            return earlier;
    }
    return std::ranges::find(verified, selected) != verified.end() ? selected : -1;
}

int EbSelectedTickGuard::keepVerifiedLearningDelay(int proposed, int planned,
                                                   std::span<int const> verified) {
    if (proposed == planned || std::ranges::find(verified, proposed) != verified.end())
        return proposed;
    return planned;
}
