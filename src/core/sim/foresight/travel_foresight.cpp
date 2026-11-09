#include "travel_foresight.hpp"
#include <algorithm>
#include <cmath>

float EbTravelForesight::aheadXFromSpeed(float speedMul, int limit, float simDt, int slack) {
    float speed = speedMul;
    if (!(speed > 0.f) || !std::isfinite(speed))
        speed = 1.f;
    if (speed < 0.25f)
        speed = 0.25f;
    if (speed > 4.f)
        speed = 4.f;

    int const ticks = std::max(0, limit) + std::max(0, slack);
    float deltaTime = simDt;
    if (!(deltaTime > 0.f) || !std::isfinite(deltaTime))
        deltaTime = EbTravelForesight::kNominalDt;

    // px/s at 1x from the old per-tick constant, then distance = v * time.
    float const pxPerSec1x = EbTravelForesight::kBasePxPerTickAt1x / EbTravelForesight::kNominalDt;
    float const timeSec = static_cast<float>(ticks) * deltaTime;
    float ahead = speed * pxPerSec1x * timeSec + EbTravelForesight::kAheadMargin;
    if (!std::isfinite(ahead))
        ahead = EbTravelForesight::kAheadMin;
    return std::clamp(ahead, EbTravelForesight::kAheadMin, EbTravelForesight::kAheadMax);
}

float EbTravelForesight::aheadX(PlayerObject* player, int limit, float simDt, int slack) {
    float speed = 1.f;
    if (player) {
        speed = player->m_playerSpeed;
    }
    return EbTravelForesight::aheadXFromSpeed(speed, limit, simDt, slack);
}

float EbTravelForesight::yReachForPlayer(PlayerObject* player) {
    if (!player)
        return 90.f;
    if (player->m_isShip || player->m_isDart || player->m_isSwing) {
        return 120.f;
    }
    return 90.f;
}

bool EbTravelForesight::isOrbLike(GameObjectType type) {
    switch (type) {
    case GameObjectType::YellowJumpRing:
    case GameObjectType::PinkJumpRing:
    case GameObjectType::RedJumpRing:
    case GameObjectType::GravityRing:
    case GameObjectType::GreenRing:
    case GameObjectType::DropRing:
    case GameObjectType::DashRing:
    case GameObjectType::GravityDashRing:
    case GameObjectType::SpiderOrb:
    case GameObjectType::CustomRing:
        return true;
    default:
        return false;
    }
}

bool EbTravelForesight::isPadLike(GameObjectType type) {
    switch (type) {
    case GameObjectType::YellowJumpPad:
    case GameObjectType::PinkJumpPad:
    case GameObjectType::RedJumpPad:
    case GameObjectType::GravityPad:
    case GameObjectType::SpiderPad:
        return true;
    default:
        return false;
    }
}

bool EbTravelForesight::isOrbOrPadLike(GameObjectType type) {
    return EbTravelForesight::isOrbLike(type) || EbTravelForesight::isPadLike(type);
}

bool EbTravelForesight::isUsableOrbOrPad(GameObject* object) {
    if (!object)
        return false;
    if (!EbTravelForesight::isOrbOrPadLike(object->m_objectType))
        return false;
    if (object->m_isActivated)
        return false;
    if (auto* ring = geode::prelude::typeinfo_cast<RingObject*>(object)) {
        if (ring->m_claimTouch)
            return false;
    }
    return true;
}

bool EbTravelForesight::scanOrbOrPadInCone(PlayLayer* layer, PlayerObject* player, float aheadX,
                                         float yReach) {
    if (!layer || !player || !layer->m_objectLayer)
        return false;
    if (!(aheadX > 0.f) || !std::isfinite(aheadX))
        return false;
    if (!(yReach > 0.f) || !std::isfinite(yReach))
        return false;

    float const playerX = player->getPositionX();
    float const playerY = player->getPositionY();

    auto* children = layer->m_objectLayer->getChildren();
    if (!children)
        return false;

    for (unsigned index = 0; index < children->count(); ++index) {
        auto* object = geode::prelude::typeinfo_cast<GameObject*>(children->objectAtIndex(index));
        if (!EbTravelForesight::isUsableOrbOrPad(object))
            continue;

        float const objectX = object->getPositionX();
        float const objectY = object->getPositionY();
        if (objectX < playerX - EbTravelForesight::kBehindSlop)
            continue;
        if (objectX > playerX + aheadX)
            continue;
        if (std::fabs(objectY - playerY) > yReach)
            continue;
        return true;
    }
    return false;
}

bool EbTravelForesight::hasOrbOrPadAhead(PlayLayer* layer, PlayerObject* player, int limit,
                                       float simDt, int slack) {
    if (!layer || !player || simDt <= 0.f)
        return false;
    float const ahead = EbTravelForesight::aheadX(player, limit, simDt, slack);
    float const yReach = EbTravelForesight::yReachForPlayer(player);
    return EbTravelForesight::scanOrbOrPadInCone(layer, player, ahead, yReach);
}

bool EbTravelForesight::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* label) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += ", ";
        report += label;
    };

    // Type tables
    expect(EbTravelForesight::isOrbLike(GameObjectType::YellowJumpRing), "orb-yellow");
    expect(EbTravelForesight::isOrbLike(GameObjectType::DashRing), "orb-dash");
    expect(!EbTravelForesight::isOrbLike(GameObjectType::Hazard), "orb-not-hazard");
    expect(EbTravelForesight::isPadLike(GameObjectType::YellowJumpPad), "pad-yellow");
    expect(EbTravelForesight::isPadLike(GameObjectType::SpiderPad), "pad-spider");
    expect(!EbTravelForesight::isPadLike(GameObjectType::YellowJumpRing), "pad-not-ring");
    expect(EbTravelForesight::isOrbOrPadLike(GameObjectType::PinkJumpPad) &&
               EbTravelForesight::isOrbOrPadLike(GameObjectType::PinkJumpRing),
           "orb-or-pad");

    // 1x, limit 16, slack 2, nominal dt → near legacy (16+8)*5.5 = 132, with margin.
    float const a16 = EbTravelForesight::aheadXFromSpeed(1.f, 16, EbTravelForesight::kNominalDt, 2);
    expect(a16 >= 60.f && a16 <= 320.f, "ahead-16-range");
    // ticks=18 → 18*5.5 + 16 = 115
    expect(std::fabs(a16 - 115.f) < 1.f, "ahead-16-nominal");

    // 2x portal should look farther than 1x (until clamp).
    float const a16x2 = EbTravelForesight::aheadXFromSpeed(2.f, 16, EbTravelForesight::kNominalDt, 2);
    expect(a16x2 > a16 + 20.f, "ahead-2x-farther");

    // Longer window → farther (unless both hit max).
    float const a8 = EbTravelForesight::aheadXFromSpeed(1.f, 8, EbTravelForesight::kNominalDt, 2);
    float const a32 = EbTravelForesight::aheadXFromSpeed(1.f, 32, EbTravelForesight::kNominalDt, 2);
    expect(a32 >= a8, "ahead-longer-window");

    // Larger simDt (more time per tick) → farther look for same tick count.
    float const aSlow = EbTravelForesight::aheadXFromSpeed(1.f, 16, 1.f / 120.f, 2);
    expect(aSlow > a16, "ahead-dt-aware");

    // Clamp floor / ceiling
    float const tiny = EbTravelForesight::aheadXFromSpeed(0.25f, 0, EbTravelForesight::kNominalDt, 0);
    expect(tiny == EbTravelForesight::kAheadMin, "ahead-min-clamp");
    float const huge = EbTravelForesight::aheadXFromSpeed(4.f, 32, 1.f / 60.f, 8);
    expect(huge == EbTravelForesight::kAheadMax, "ahead-max-clamp");

    // Bad inputs
    expect(EbTravelForesight::aheadXFromSpeed(0.f, 16, EbTravelForesight::kNominalDt, 2) == a16,
           "ahead-bad-speed-as-1");
    expect(EbTravelForesight::aheadXFromSpeed(1.f, 16, 0.f, 2) == a16, "ahead-bad-dt-nominal");
    expect(EbTravelForesight::aheadXFromSpeed(1.f, -4, EbTravelForesight::kNominalDt, -2) ==
               EbTravelForesight::kAheadMin,
           "ahead-neg-ticks-min");

    expect(EbTravelForesight::yReachForPlayer(nullptr) == 90.f, "y-null");
    expect(!EbTravelForesight::isUsableOrbOrPad(nullptr), "usable-null");
    expect(!EbTravelForesight::hasOrbOrPadAhead(nullptr, nullptr, 16, EbTravelForesight::kNominalDt, 2),
           "scan-null");
    expect(!EbTravelForesight::scanOrbOrPadInCone(nullptr, nullptr, 100.f, 90.f), "cone-null");

    // Legacy-ish: old formula (limit+8)*5.5 at limit 16 = 132; our slack=2 path is 115.
    // With slack=8 we match old (limit+8): 16+8 ticks → 24*5.5+16 = 148.
    float const legacySlack =
        EbTravelForesight::aheadXFromSpeed(1.f, 16, EbTravelForesight::kNominalDt, 8);
    expect(std::fabs(legacySlack - (24.f * 5.5f + 16.f)) < 1.f, "ahead-slack8");

    if (fails == 0) {
        report = "TravelForesight selfCheck OK";
        return true;
    }
    report = std::string("TravelForesight selfCheck fails: ") + report;
    return false;
}
