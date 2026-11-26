#ifndef SHADOW_ISOLATION_CONTRACTS_HPP
#define SHADOW_ISOLATION_CONTRACTS_HPP

#include <Geode/Geode.hpp>
#include <string>

// AP-NL1 — Shared Live/Autoplay ghost + HardStreak isolation contracts.
// Byte layouts from Nova RE are irrelevant here; ownership and mutation rules are.
class EbShadowIsolationContracts {
public:
    // Mod-scoped markers (stable keys for other mods / PlanGold).
    static char const* simGhostPlayerKey();
    static char const* simGhostTrailKey();
    static char const* ghostRootId();

    static void markSimGhostPlayer(PlayerObject* player);
    static bool isSimGhostPlayer(PlayerObject* player);

    static void markSimGhostTrail(HardStreak* trail);
    static bool isSimGhostTrail(HardStreak* trail);

    // True when destination's wave trail is exclusive of the live source trail.
    static bool trailsAreExclusive(PlayerObject* destination, PlayerObject* source);

    // Ghost HardStreak must not live under PlayLayer::m_objectLayer.
    static bool trailParentIsSafe(HardStreak* trail, cocos2d::CCNode* objectLayer);

    // Ghost root must be a PlayLayer child, never objectLayer.
    static bool ghostRootPlacementOk(cocos2d::CCNode* ghostRoot, PlayLayer* layer);

    static bool selfCheck(std::string& report);

private:
    EbShadowIsolationContracts() = delete;
};

#endif
