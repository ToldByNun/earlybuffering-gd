#ifndef EARLYBUFFER_CORE_BAKEVERIFIER_HPP
#define EARLYBUFFER_CORE_BAKEVERIFIER_HPP

class PlayLayer;
class PlayerObject;

// Live re-check of a BakeMap delay before Hybrid/Replay/Brute uses it.
// One runOne: must clearThreat and not die before the action.
class BakeVerifier {
public:
    struct Result {
        bool ok = false;
        bool clearsThreat = false;
        bool diedBeforeAction = false;
        bool budgetHit = false;
    };

    // delayTicks: 0 = fire now (already inside bake window).
    static Result verify(
        PlayLayer* layer, PlayerObject* player, bool down,
        int delayTicks, int limit, float dt, float firstDt
    );
};

#endif // EARLYBUFFER_CORE_BAKEVERIFIER_HPP
