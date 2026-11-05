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
    if (hit.mode != kind || hit.dual != PlayLayer::get()->m_gameState.m_isDualMode) {
        return AssistReason::PassRouteOff;
    }
    float const modeHalf = ModeAssist::forKind(kind).routeCorridorHalfY()
        * StageConfig::routeCorridorMultiplier();
    float const tolerance = std::max(modeHalf, StageConfig::routeOffRouteThreshold());
    return std::fabs(player->getPositionY() - hit.y) > tolerance
        ? AssistReason::PassRouteOff : AssistReason::None;
}
