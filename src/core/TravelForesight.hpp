#ifndef TRAVEL_FORESIGHT_HPP
#define TRAVEL_FORESIGHT_HPP

#include <Geode/Geode.hpp>
#include <string>

// Dt-/speed-aware look-ahead for orbs and jump pads (miss-assist foresight).
// Pure helpers + layer scan; no namespaces.
class TravelForesight {
public:
    // Calibrated so 1x @ 240 TPS ≈ legacy (limit+8)*5.5 at typical windows.
    static constexpr float kBasePxPerTickAt1x = 5.5f;
    static constexpr float kNominalDt = 1.f / 240.f;
    static constexpr float kAheadMargin = 16.f;
    static constexpr float kAheadMin = 60.f;
    static constexpr float kAheadMax = 320.f;
    static constexpr float kBehindSlop = 10.f;

    // Travel cone length in world X for limit+slack ticks at current simDt / speed.
    // speedMul: portal factor (PlayerObject::m_playerSpeed); clamped.
    static float aheadXFromSpeed(float speedMul, int limit, float simDt, int slack);

    // Reads player speed / mode; slack < 0 → 0.
    static float aheadX(PlayerObject* player, int limit, float simDt, int slack);

    static float yReachForPlayer(PlayerObject* player);

    static bool isOrbLike(GameObjectType type);
    static bool isPadLike(GameObjectType type);
    static bool isOrbOrPadLike(GameObjectType type);

    // Skip spent rings / claimed touch.
    static bool isUsableOrbOrPad(GameObject* obj);

    // True if an orb or pad sits in [px - behindSlop, px + aheadX] within yReach.
    static bool hasOrbOrPadAhead(
        PlayLayer* layer,
        PlayerObject* player,
        int limit,
        float simDt,
        int slack
    );

    // Same scan with an explicit ahead distance (tests / callers that precomputed).
    static bool scanOrbOrPadInCone(
        PlayLayer* layer,
        PlayerObject* player,
        float aheadX,
        float yReach
    );

    static bool selfCheck(std::string& report);

private:
    TravelForesight() = default;
};

#endif // TRAVEL_FORESIGHT_HPP
