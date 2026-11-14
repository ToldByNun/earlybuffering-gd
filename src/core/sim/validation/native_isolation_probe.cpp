#include "native_isolation_probe.hpp"
#include "../shadow_simulator.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <cmath>
#include <sstream>

thread_local bool EbNativeIsolationProbe::s_creating = false;
thread_local EbShadowSimulator* EbNativeIsolationProbe::s_active = nullptr;

bool EbNativeIsolationProbe::creatingWorld() {
    return s_creating;
}
EbShadowSimulator* EbNativeIsolationProbe::activeSimulator() {
    return s_active;
}

std::string EbNativeIsolationProbe::run() {
    auto* manager = GameManager::sharedState();
    if (!manager || manager->m_playLayer || manager->m_gameLayer)
        return "Run Native Isolation from the main menu after leaving the level.";

    auto* level = GJGameLevel::create();
    level->m_levelName = "EarlyBuffer isolation";
    level->m_levelString = "kA2,0,kA3,0,kA4,0,kA6,0,kA7,0,kA8,0,kA9,0,kA10,0;"
                           "1,1,2,150,3,90;1,1,2,180,3,90;1,1,2,210,3,90;"
                           "1,8,2,300,3,15;";
    auto* savedPlay = manager->m_playLayer;
    auto* savedGame = manager->m_gameLayer;
    EbShadowSimulator firstSim;
    EbShadowSimulator secondSim;
    firstSim.m_simulating = true;
    secondSim.m_simulating = true;
    s_active = &firstSim;
    s_creating = true;
    auto* first = PlayLayer::create(level, false, false);
    s_active = &secondSim;
    auto* second = PlayLayer::create(level, false, false);
    s_creating = false;
    s_active = nullptr;
    manager->m_playLayer = savedPlay;
    manager->m_gameLayer = savedGame;
    if (!first || !second || !first->m_player1 || !second->m_player1)
        return "Native isolation failed: could not initialize both fixture worlds.";

    first->unscheduleAllSelectors();
    second->unscheduleAllSelectors();
    first->stopAllActions();
    second->stopAllActions();

    manager->m_playLayer = first;
    manager->m_gameLayer = first;
    s_active = &firstSim;
    firstSim.attach(first);
    manager->m_playLayer = second;
    manager->m_gameLayer = second;
    s_active = &secondSim;
    secondSim.attach(second);
    if (!firstSim.m_shadow || !secondSim.m_shadow) {
        firstSim.detach(true);
        secondSim.detach(true);
        s_active = nullptr;
        manager->m_playLayer = savedPlay;
        manager->m_gameLayer = savedGame;
        return "Native isolation failed: fixture ghosts could not initialize.";
    }
    firstSim.m_simulating = true;
    secondSim.m_simulating = true;
    int differences = 0;
    int collisions = 0;
    int firstDifference = -1;
    for (int repetition = 0; repetition < 1000; ++repetition) {
        EbShadowSimulator::tryRemoveFromSection(first, firstSim.m_shadow);
        EbShadowSimulator::tryRemoveFromSection(second, secondSim.m_shadow);
        s_active = &firstSim;
        firstSim.copyPlayerState(firstSim.m_shadow, first->m_player1);
        s_active = &secondSim;
        secondSim.copyPlayerState(secondSim.m_shadow, second->m_player1);
        firstSim.m_dead = secondSim.m_dead = false;
        firstSim.m_deathObject = secondSim.m_deathObject = nullptr;
        for (int tick = 0; tick < 96; ++tick) {
            manager->m_playLayer = first;
            manager->m_gameLayer = first;
            s_active = &firstSim;
            firstSim.stepPhysics(first, firstSim.m_shadow, 1.f / 240.f, 1.f / 240.f);
            manager->m_playLayer = second;
            manager->m_gameLayer = second;
            s_active = &secondSim;
            secondSim.stepPhysics(second, secondSim.m_shadow, 1.f / 240.f, 1.f / 240.f);
            auto* left = firstSim.m_shadow;
            auto* right = secondSim.m_shadow;
            bool const finite =
                std::isfinite(left->getPositionX()) && std::isfinite(left->getPositionY()) &&
                std::isfinite(right->getPositionX()) && std::isfinite(right->getPositionY());
            int const leftObject = firstSim.m_deathObject ? firstSim.m_deathObject->m_objectID : -1;
            int const rightObject =
                secondSim.m_deathObject ? secondSim.m_deathObject->m_objectID : -1;
            bool const equal = finite && left->getPosition() == right->getPosition() &&
                               left->m_yVelocity == right->m_yVelocity &&
                               left->m_isOnGround == right->m_isOnGround &&
                               left->m_isUpsideDown == right->m_isUpsideDown &&
                               firstSim.m_dead == secondSim.m_dead && leftObject == rightObject;
            if (!equal) {
                ++differences;
                if (firstDifference < 0)
                    firstDifference = tick;
                break;
            }
            if (firstSim.m_dead) {
                ++collisions;
                break;
            }
        }
    }
    first->cleanup();
    second->cleanup();
    firstSim.m_simulating = secondSim.m_simulating = false;
    s_active = &firstSim;
    firstSim.detach(true);
    s_active = &secondSim;
    secondSim.detach(true);
    s_active = nullptr;
    manager->m_playLayer = savedPlay;
    manager->m_gameLayer = savedGame;
    std::ostringstream report;
    report << "AP-P02 serial fixture, 1000 repetitions" << std::endl
           << "Differences: " << differences << std::endl
           << "First divergent tick: " << firstDifference << std::endl
           << "Matching collision deaths: " << collisions << std::endl;
    if (collisions == 0)
        report << "INCONCLUSIVE: fixture did not demonstrate a collision death." << std::endl;
    report << "Global PlayLayer/GameLayer pointers were switched serially." << std::endl
           << "Worker execution and full global-state isolation remain unverified.";
    auto const path = geode::Mod::get()->getSaveDir() / "native-isolation.txt";
    auto const written = geode::utils::file::writeString(path, report.str());
    if (written.isErr())
        report << "\nCould not save report.";
    else
        report << "\nSaved: " << path.string();
    return report.str();
}
