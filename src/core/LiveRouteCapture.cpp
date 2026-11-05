#include "LiveRouteCapture.hpp"
#include "LevelFingerprint.hpp"
#include "LevelProfile.hpp"
#include "RoutePathBuilder.hpp"
#include "RoutePathStore.hpp"
#include "../debug/AssistLog.hpp"
#include <Geode/ui/Notification.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {
    PlayLayer* owner = nullptr;
    bool capturing = false;
    std::vector<ShadowSimulator::BakeFrameTrace> poses;

    void stopWithError(std::string const& reason) {
        LiveRouteCapture::cancel();
        AssistLog::info("ROUTE capture: " + reason);
        Notification::create(reason, NotificationIcon::Error, 5.f)->show();
    }
}

void LiveRouteCapture::arm(PlayLayer* layer) {
    cancel();
    owner = layer;
}

bool LiveRouteCapture::armed() { return owner != nullptr; }
bool LiveRouteCapture::recording(GJBaseGameLayer* layer) {
    return capturing && owner == layer;
}

void LiveRouteCapture::resetAttempt(PlayLayer* layer) {
    if (!owner || owner != layer) return;
    poses.clear();
    capturing = true;
}

void LiveRouteCapture::cancel() {
    owner = nullptr;
    capturing = false;
    poses = decltype(poses){};
}

void LiveRouteCapture::sample(PlayLayer* layer) {
    auto* player = layer->m_player1;
    if (!recording(layer) || !player || layer->m_levelEndAnimationStarted) return;
    if (player->m_isDead) {
        poses.clear();
        capturing = false;
        return;
    }
    // Bound a stalled or indefinitely long attempt. Never publish a truncated path.
    if (poses.size() >= 512 * 1024) {
        stopWithError("Route capture exceeded the physics tick limit.");
        return;
    }
    ShadowSimulator::BakeFrameTrace p;
    p.frame = static_cast<int>(poses.size());
    p.x = player->getPositionX();
    p.y = player->getPositionY();
    p.yVel = static_cast<float>(player->m_yVelocity);
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.yVel)) {
        stopWithError("Route capture received an invalid player position.");
        return;
    }
    p.mode = static_cast<int>(StageConfig::kindOf(player));
    p.upsideDown = player->m_isUpsideDown;
    p.mini = player->m_vehicleSize < 1.f;
    p.dual = layer->m_gameState.m_isDualMode;
    poses.push_back(p);
}

void LiveRouteCapture::complete(PlayLayer* layer) {
    if (!recording(layer) || !layer->m_player1 || layer->m_player1->m_isDead) return;
    auto route = RoutePathBuilder::buildFromTraces(poses);
    if (route.empty() || !std::is_sorted(route.samples.begin(), route.samples.end(),
            [](auto const& a, auto const& b) { return a.x < b.x; })) {
        stopWithError("Route needs a forward path with at least two samples.");
        return;
    }
    int const shard = LevelFingerprint::startXq(poses.front().x);
    route.source = "live-completion";
    auto result = RoutePathStore::get().save(LevelProfile::makeKey(layer->m_level), route, shard);
    if (result.isErr()) {
        stopWithError(fmt::format("Could not save route: {}", result.unwrapErr()));
        return;
    }
    cancel();
    auto message = fmt::format("Route saved ({} samples)", route.sampleCount());
    AssistLog::info(message);
    Notification::create(message, NotificationIcon::Success, 5.f)->show();
}
