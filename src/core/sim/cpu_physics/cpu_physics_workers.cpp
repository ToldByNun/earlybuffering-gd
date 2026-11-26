#include "cpu_physics_workers.hpp"
#include "../runtime/shadow_isolation_contracts.hpp"
#include "../shadow_simulator.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <fmt/format.h>
#include <mutex>
#include <thread>
#include <vector>

static std::mutex s_workerMutex;
static std::vector<PlayerObject*> s_workerGhosts;
static PlayerObject* s_templateGhost = nullptr;
static PlayLayer* s_workerHost = nullptr;

unsigned EbCpuPhysicsWorkers::workerCount() {
    unsigned const hw = std::thread::hardware_concurrency();
    if (hw <= 1u)
        return 1u;
    return std::clamp(hw - 1u, 2u, 8u);
}

bool EbCpuPhysicsWorkers::ensure(PlayLayer* layer) {
    std::lock_guard lock(s_workerMutex);
    if (!layer)
        return false;
    auto& sim = EbShadowSimulator::get();
    if (!sim.isAttachedTo(layer) || !sim.m_ghostRoot)
        return false;
    unsigned const want = workerCount();
    if (s_workerHost == layer && s_workerGhosts.size() == want && s_templateGhost &&
        s_templateGhost->getParent() == sim.m_ghostRoot) {
        bool ok = true;
        for (auto* ghost : s_workerGhosts) {
            if (!ghost || ghost->getParent() != sim.m_ghostRoot)
                ok = false;
        }
        if (ok)
            return true;
    }

    for (auto* ghost : s_workerGhosts) {
        if (ghost && ghost->getParent())
            ghost->removeFromParentAndCleanup(true);
    }
    s_workerGhosts.clear();
    if (s_templateGhost && s_templateGhost->getParent())
        s_templateGhost->removeFromParentAndCleanup(true);
    s_templateGhost = nullptr;
    s_workerHost = layer;

    auto makeGhost = [&](char const* id) -> PlayerObject* {
        auto* player = PlayerObject::create(1, 1, layer, layer, true);
        if (!player)
            return nullptr;
        player->setPosition({0.f, 105.f});
        player->setID(id);
        EbShadowIsolationContracts::markSimGhostPlayer(player);
        sim.m_ghostRoot->addChild(player);
        player->m_waveTrail = nullptr;
        if (auto* trail = HardStreak::create()) {
            EbShadowIsolationContracts::markSimGhostTrail(trail);
            sim.m_ghostRoot->addChild(trail);
            player->m_waveTrail = trail;
        }
        EbShadowSimulator::hideGhost(player);
        return player;
    };

    s_templateGhost = makeGhost("earlybuffer-cpu-template");
    if (!s_templateGhost)
        return false;

    s_workerGhosts.reserve(want);
    for (unsigned index = 0; index < want; ++index) {
        auto* player =
            makeGhost(fmt::format("earlybuffer-cpu-worker-{}", index).c_str());
        if (!player)
            break;
        s_workerGhosts.push_back(player);
    }
    return !s_workerGhosts.empty() && s_templateGhost != nullptr;
}

void EbCpuPhysicsWorkers::clear() {
    std::lock_guard lock(s_workerMutex);
    for (auto* ghost : s_workerGhosts) {
        if (ghost && ghost->getParent())
            ghost->removeFromParentAndCleanup(true);
    }
    s_workerGhosts.clear();
    if (s_templateGhost && s_templateGhost->getParent())
        s_templateGhost->removeFromParentAndCleanup(true);
    s_templateGhost = nullptr;
    s_workerHost = nullptr;
}

std::vector<PlayerObject*> const& EbCpuPhysicsWorkers::ghosts() {
    return s_workerGhosts;
}

PlayerObject* EbCpuPhysicsWorkers::templateGhost() {
    return s_templateGhost;
}

void EbCpuPhysicsWorkers::seedFrom(PlayLayer* layer, PlayerObject* real) {
    if (!layer || !real)
        return;
    if (!ensure(layer))
        return;
    std::lock_guard lock(s_workerMutex);
    auto& sim = EbShadowSimulator::get();
    if (s_templateGhost) {
        sim.copyPlayerState(s_templateGhost, real);
        EbShadowSimulator::hideGhost(s_templateGhost);
    }
    for (auto* ghost : s_workerGhosts) {
        if (!ghost)
            continue;
        sim.copyPlayerState(ghost, real);
        EbShadowSimulator::hideGhost(ghost);
    }
}

bool EbCpuPhysicsWorkers::selfCheck(std::string& report) {
    report.clear();
    if (workerCount() < 1u) {
        report = "CpuPhysicsWorkers selfCheck fails: workers";
        return false;
    }
    report = "CpuPhysicsWorkers selfCheck OK";
    return true;
}
