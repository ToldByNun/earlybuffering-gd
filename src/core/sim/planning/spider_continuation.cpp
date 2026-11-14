#include "spider_continuation.hpp"
#include <algorithm>

int EbSpiderContinuation::probe(int limit, int index) {
    if (limit < 1 || index < 0 || index > 3)
        return -1;
    int const early = std::max(1, limit / 4);
    if (index == 0)
        return early;
    if (index == 1)
        return std::min(limit, early + 1);
    return index == 2 ? std::max(1, limit / 2) : limit;
}

bool EbSpiderContinuation::allowed(int baselineDeath, int delay, int delayedDeath, int nextPress) {
    return baselineDeath >= 0 && delay > 0 && delayedDeath >= baselineDeath + 8 &&
           nextPress > delay + 1 && nextPress < delayedDeath;
}

bool EbSpiderContinuation::verified(bool died, bool unknown, int ticksRan, int horizon,
                                    bool followupJumped, int nextPress) {
    return !died && !unknown && followupJumped && horizon > 0 && ticksRan >= horizon &&
           nextPress >= 0 && ticksRan - nextPress >= 24;
}
