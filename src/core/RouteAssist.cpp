#include "RouteAssist.hpp"

#include "ModeAssist.hpp"
#include "RoutePathStore.hpp"
#include "../config/StageConfig.hpp"

#include <cmath>

AssistReason RouteAssist::gate(PlayerObject* player, bool player1) {
    if (!player || !player1) return AssistReason::PassRouteOff;
    auto const& store = RoutePathStore::get();
    if (!store.loaded() || store.path().empty() || !store.path().parityOk) {
        return AssistReason::PassRouteMissing;
    }
    RouteSample hit;
    if (!store.path().lookupX(player->getPositionX(), hit)) return AssistReason::PassRouteOff;
    auto const kind = StageConfig::kindOf(player);
    auto* playLayer = PlayLayer::get();
    if (!playLayer || hit.mode != kind || hit.dual != playLayer->m_gameState.m_isDualMode) {
        return AssistReason::PassRouteOff;
    }
    // The user-facing corridor setting must affect the route admission gate.
    // The old max(modeHalf, offRoute) made Strict/Normal/Loose inert at the
    // default 28 px threshold because every mode corridor is narrower.
    float const multiplier = StageConfig::routeCorridorMultiplier();
    float const storedHalf = std::max(
        std::fabs(hit.y - hit.yLo), std::fabs(hit.yHi - hit.y)
    );
    float const modeHalf = std::max(
        storedHalf, ModeAssist::forKind(kind).routeCorridorHalfY()
    );
    float const tolerance = std::max(
        modeHalf * multiplier,
        StageConfig::routeOffRouteThreshold() * multiplier
    );
    return std::fabs(player->getPositionY() - hit.y) > tolerance
        ? AssistReason::PassRouteOff : AssistReason::None;
}
