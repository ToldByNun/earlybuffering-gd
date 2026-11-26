#include "shadow_isolation_contracts.hpp"
#include "live_wave_trail_guard.hpp"

char const* EbShadowIsolationContracts::simGhostPlayerKey() {
    return "audesign.earlybuffer/sim-ghost";
}

char const* EbShadowIsolationContracts::simGhostTrailKey() {
    return "audesign.earlybuffer/sim-ghost-trail";
}

char const* EbShadowIsolationContracts::ghostRootId() {
    // Lazy: Geode `_spr` may need Mod::get() after load.
    static std::string id;
    if (id.empty())
        id = "earlybuffer-ghost-root"_spr;
    return id.c_str();
}

void EbShadowIsolationContracts::markSimGhostPlayer(PlayerObject* player) {
    if (!player)
        return;
    player->setUserObject(simGhostPlayerKey(), cocos2d::CCBool::create(true));
}

bool EbShadowIsolationContracts::isSimGhostPlayer(PlayerObject* player) {
    if (!player)
        return false;
    // Avoid userObject during node teardown (parent already cleared).
    if (!player->getParent())
        return false;
    return player->getUserObject(simGhostPlayerKey()) != nullptr;
}

void EbShadowIsolationContracts::markSimGhostTrail(HardStreak* trail) {
    if (!trail)
        return;
    // Never mark the live stroke — that would mute the real wave trail.
    if (EbLiveWaveTrailGuard::isProtected(trail) || EbLiveWaveTrailGuard::isLivePlayerTrail(trail))
        return;
    trail->setUserObject(simGhostTrailKey(), cocos2d::CCBool::create(true));
    trail->m_drawStreak = false;
    trail->setVisible(false);
    trail->setOpacity(0);
}

bool EbShadowIsolationContracts::isSimGhostTrail(HardStreak* trail) {
    if (!trail)
        return false;
    if (EbLiveWaveTrailGuard::isProtected(trail) || EbLiveWaveTrailGuard::isLivePlayerTrail(trail))
        return false;
    // Skip marker read while the streak is leaving the graph (quit teardown).
    if (!trail->getParent())
        return false;
    return trail->getUserObject(simGhostTrailKey()) != nullptr;
}

bool EbShadowIsolationContracts::trailsAreExclusive(PlayerObject* destination, PlayerObject* source) {
    if (!destination || !source)
        return false;
    auto* destTrail = destination->m_waveTrail;
    auto* srcTrail = source->m_waveTrail;
    if (!destTrail)
        return true;
    if (destTrail == srcTrail)
        return false;
    if (EbLiveWaveTrailGuard::isProtected(destTrail) ||
        EbLiveWaveTrailGuard::isLivePlayerTrail(destTrail))
        return false;
    return true;
}

bool EbShadowIsolationContracts::trailParentIsSafe(HardStreak* trail,
                                                    cocos2d::CCNode* objectLayer) {
    if (!trail)
        return true;
    if (!objectLayer)
        return true;
    cocos2d::CCNode* parent = trail->getParent();
    while (parent) {
        if (parent == objectLayer)
            return false;
        parent = parent->getParent();
    }
    return true;
}

bool EbShadowIsolationContracts::ghostRootPlacementOk(cocos2d::CCNode* ghostRoot,
                                                      PlayLayer* layer) {
    if (!ghostRoot || !layer)
        return false;
    if (ghostRoot->getParent() != layer)
        return false;
    if (layer->m_objectLayer && ghostRoot == layer->m_objectLayer)
        return false;
    if (layer->m_objectLayer && ghostRoot->getParent() == layer->m_objectLayer)
        return false;
    return true;
}

bool EbShadowIsolationContracts::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(simGhostPlayerKey() != nullptr && simGhostPlayerKey()[0] != '\0', "ghost-player-key");
    expect(simGhostTrailKey() != nullptr && simGhostTrailKey()[0] != '\0', "ghost-trail-key");
    expect(ghostRootId() != nullptr && ghostRootId()[0] != '\0', "ghost-root-id");

    // Null-safe helpers.
    expect(!isSimGhostPlayer(nullptr), "null-player");
    expect(!isSimGhostTrail(nullptr), "null-trail");
    expect(trailsAreExclusive(nullptr, nullptr) == false, "exclusive-null");
    expect(trailParentIsSafe(nullptr, nullptr), "parent-null-safe");
    expect(!ghostRootPlacementOk(nullptr, nullptr), "root-null");

    EbLiveWaveTrailGuard::clear();
    expect(EbLiveWaveTrailGuard::refuseMutation(nullptr) == false, "refuse-null");
    expect(EbLiveWaveTrailGuard::isProtected(nullptr) == false, "protected-null");
    expect(EbLiveWaveTrailGuard::isLivePlayerTrail(nullptr) == false, "live-trail-null");

    // Key stability — PlanGold / other mods rely on these exact strings.
    expect(std::string(simGhostPlayerKey()) == "audesign.earlybuffer/sim-ghost",
           "ghost-player-key-literal");
    expect(std::string(simGhostTrailKey()) == "audesign.earlybuffer/sim-ghost-trail",
           "ghost-trail-key-literal");

    if (fails == 0) {
        report = "ShadowIsolationContracts OK (nova-shadow-isolation)";
        return true;
    }
    report = std::string("ShadowIsolationContracts fails: ") + report;
    return false;
}
