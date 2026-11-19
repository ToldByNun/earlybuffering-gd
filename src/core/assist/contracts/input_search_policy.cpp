#include "input_search_policy.hpp"
bool EbInputSearchPolicy::usesInputWindow(int mode, bool down, bool dashing) {
    // Serialized GameModeKind order: Cube, Ship, Ball, UFO, Wave, Robot, Spider, Swing.
    if (mode < 0 || mode >= 8)
        return false;
    return down || dashing || mode == 1 || mode == 4 || mode == 5 || mode == 7;
}
bool EbInputSearchPolicy::needsRescue(bool baselineDied, bool unknown, bool diedBeforeAction,
                                      int limit) {
    return baselineDied && !unknown && !diedBeforeAction && limit > 0;
}
