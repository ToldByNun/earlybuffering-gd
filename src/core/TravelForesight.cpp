#include "TravelForesight.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

float TravelForesight::aheadXFromSpeed(
    float speedMul, int limit, float simDt, int slack
) {
    float speed = speedMul;
    if (!(speed > 0.f) || !std::isfinite(speed)) speed = 1.f;
    if (speed < 0.25f) speed = 0.25f;
    if (speed > 4.f) speed = 4.f;

    int const ticks = std::max(0, limit) + std::max(0, slack);
    float dt = simDt;
    if (!(dt > 0.f) || !std::isfinite(dt)) dt = TravelForesight::kNominalDt;

    // px/s at 1x from the old per-tick constant, then distance = v * time.
    float const pxPerSec1x =
        TravelForesight::kBasePxPerTickAt1x / TravelForesight::kNominalDt;
    float const timeSec = static_cast<float>(ticks) * dt;
    float ahead = speed * pxPerSec1x * timeSec + TravelForesight::kAheadMargin;
    if (!std::isfinite(ahead)) ahead = TravelForesight::kAheadMin;
    return std::clamp(
        ahead, TravelForesight::kAheadMin, TravelForesight::kAheadMax
    );
}

float TravelForesight::aheadX(
    PlayerObject* player, int limit, float simDt, int slack
) {
    float speed = 1.f;
    if (player) {
        speed = player->m_playerSpeed;
    }
    return TravelForesight::aheadXFromSpeed(speed, limit, simDt, slack);
}

float TravelForesight::yReachForPlayer(PlayerObject* player) {
    if (!player) return 90.f;
    if (player->m_isShip || player->m_isDart || player->m_isSwing) {
        return 120.f;
    }
    return 90.f;
}

bool TravelForesight::isOrbLike(GameObjectType type) {
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

bool TravelForesight::isPadLike(GameObjectType type) {
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

bool TravelForesight::isOrbOrPadLike(GameObjectType type) {
    return TravelForesight::isOrbLike(type) || TravelForesight::isPadLike(type);
}

bool TravelForesight::isUsableOrbOrPad(GameObject* obj) {
    if (!obj) return false;
    if (!TravelForesight::isOrbOrPadLike(obj->m_objectType)) return false;
    if (obj->m_isActivated) return false;
    if (auto* ring = typeinfo_cast<RingObject*>(obj)) {
        if (ring->m_claimTouch) return false;
    }
    return true;
}

bool TravelForesight::scanOrbOrPadInCone(
    PlayLayer* layer,
    PlayerObject* player,
    float aheadX,
    float yReach
) {
    if (!layer || !player || !layer->m_objectLayer) return false;
    if (!(aheadX > 0.f) || !std::isfinite(aheadX)) return false;
    if (!(yReach > 0.f) || !std::isfinite(yReach)) return false;

    float const px = player->getPositionX();
    float const py = player->getPositionY();

    auto* children = layer->m_objectLayer->getChildren();
    if (!children) return false;

    for (unsigned i = 0; i < children->count(); ++i) {
        auto* obj = typeinfo_cast<GameObject*>(children->objectAtIndex(i));
        if (!TravelForesight::isUsableOrbOrPad(obj)) continue;

        float const ox = obj->getPositionX();
        float const oy = obj->getPositionY();
        if (ox < px - TravelForesight::kBehindSlop) continue;
        if (ox > px + aheadX) continue;
        if (std::fabs(oy - py) > yReach) continue;
        return true;
    }
    return false;
}

bool TravelForesight::hasOrbOrPadAhead(
    PlayLayer* layer,
    PlayerObject* player,
    int limit,
    float simDt,
    int slack
) {
    if (!layer || !player || simDt <= 0.f) return false;
    float const ahead = TravelForesight::aheadX(player, limit, simDt, slack);
    float const yReach = TravelForesight::yReachForPlayer(player);
    return TravelForesight::scanOrbOrPadInCone(layer, player, ahead, yReach);
}

bool TravelForesight::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* label) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += ", ";
        report += label;
    };

    // Type tables
    expect(TravelForesight::isOrbLike(GameObjectType::YellowJumpRing), "orb-yellow");
    expect(TravelForesight::isOrbLike(GameObjectType::DashRing), "orb-dash");
    expect(!TravelForesight::isOrbLike(GameObjectType::Hazard), "orb-not-hazard");
    expect(TravelForesight::isPadLike(GameObjectType::YellowJumpPad), "pad-yellow");
    expect(TravelForesight::isPadLike(GameObjectType::SpiderPad), "pad-spider");
    expect(!TravelForesight::isPadLike(GameObjectType::YellowJumpRing), "pad-not-ring");
    expect(
        TravelForesight::isOrbOrPadLike(GameObjectType::PinkJumpPad)
            && TravelForesight::isOrbOrPadLike(GameObjectType::PinkJumpRing),
        "orb-or-pad"
    );

    // 1x, limit 16, slack 2, nominal dt → near legacy (16+8)*5.5 = 132, with margin.
    float const a16 = TravelForesight::aheadXFromSpeed(
        1.f, 16, TravelForesight::kNominalDt, 2
    );
    expect(a16 >= 60.f && a16 <= 320.f, "ahead-16-range");
    // ticks=18 → 18*5.5 + 16 = 115
    expect(std::fabs(a16 - 115.f) < 1.f, "ahead-16-nominal");

    // 2x portal should look farther than 1x (until clamp).
    float const a16x2 = TravelForesight::aheadXFromSpeed(
        2.f, 16, TravelForesight::kNominalDt, 2
    );
    expect(a16x2 > a16 + 20.f, "ahead-2x-farther");

    // Longer window → farther (unless both hit max).
    float const a8 = TravelForesight::aheadXFromSpeed(
        1.f, 8, TravelForesight::kNominalDt, 2
    );
    float const a32 = TravelForesight::aheadXFromSpeed(
        1.f, 32, TravelForesight::kNominalDt, 2
    );
    expect(a32 >= a8, "ahead-longer-window");

    // Larger simDt (more time per tick) → farther look for same tick count.
    float const aSlow = TravelForesight::aheadXFromSpeed(1.f, 16, 1.f / 120.f, 2);
    expect(aSlow > a16, "ahead-dt-aware");

    // Clamp floor / ceiling
    float const tiny = TravelForesight::aheadXFromSpeed(0.25f, 0, TravelForesight::kNominalDt, 0);
    expect(tiny == TravelForesight::kAheadMin, "ahead-min-clamp");
    float const huge = TravelForesight::aheadXFromSpeed(4.f, 32, 1.f / 60.f, 8);
    expect(huge == TravelForesight::kAheadMax, "ahead-max-clamp");

    // Bad inputs
    expect(
        TravelForesight::aheadXFromSpeed(0.f, 16, TravelForesight::kNominalDt, 2)
            == a16,
        "ahead-bad-speed-as-1"
    );
    expect(
        TravelForesight::aheadXFromSpeed(1.f, 16, 0.f, 2) == a16,
        "ahead-bad-dt-nominal"
    );
    expect(
        TravelForesight::aheadXFromSpeed(1.f, -4, TravelForesight::kNominalDt, -2)
            == TravelForesight::kAheadMin,
        "ahead-neg-ticks-min"
    );

    expect(TravelForesight::yReachForPlayer(nullptr) == 90.f, "y-null");
    expect(!TravelForesight::isUsableOrbOrPad(nullptr), "usable-null");
    expect(
        !TravelForesight::hasOrbOrPadAhead(nullptr, nullptr, 16, TravelForesight::kNominalDt, 2),
        "scan-null"
    );
    expect(
        !TravelForesight::scanOrbOrPadInCone(nullptr, nullptr, 100.f, 90.f),
        "cone-null"
    );

    // Legacy-ish: old formula (limit+8)*5.5 at limit 16 = 132; our slack=2 path is 115.
    // With slack=8 we match old (limit+8): 16+8 ticks → 24*5.5+16 = 148.
    float const legacySlack = TravelForesight::aheadXFromSpeed(
        1.f, 16, TravelForesight::kNominalDt, 8
    );
    expect(std::fabs(legacySlack - (24.f * 5.5f + 16.f)) < 1.f, "ahead-slack8");

    if (fails == 0) {
        report = "TravelForesight selfCheck OK";
        return true;
    }
    report = std::string("TravelForesight selfCheck fails: ") + report;
    return false;
}
