#include "native_isolation_probe.hpp"
#include "../orbs/orb_stack_resolver.hpp"
#include "../runtime/native_memory_guard.hpp"
#include "../shadow_simulator.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <latch>
#include <sstream>
#include <thread>
#include <vector>

thread_local bool EbNativeIsolationProbe::s_creating = false;
thread_local EbShadowSimulator* EbNativeIsolationProbe::s_active = nullptr;

bool EbNativeIsolationProbe::creatingWorld() {
    return s_creating;
}
EbShadowSimulator* EbNativeIsolationProbe::activeSimulator() {
    return s_active;
}

bool EbNativeIsolationProbe::TickResult::matches(TickResult const& other) const {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(other.x) &&
           std::isfinite(other.y) && x == other.x && y == other.y && velocity == other.velocity &&
           grounded == other.grounded && upsideDown == other.upsideDown && dead == other.dead &&
           ringCount == other.ringCount && orbContact == other.orbContact &&
           jumpHeld == other.jumpHeld && orbOverlap == other.orbOverlap &&
           movingX == other.movingX && movingY == other.movingY && objectId == other.objectId &&
           objectX == other.objectX && objectY == other.objectY;
}

void EbNativeIsolationProbe::moveFixtureObject(PlayLayer* layer, GameObject* object,
                                               cocos2d::CCPoint const& position) {
    EbTriggerPose pose;
    pose.x = position.x;
    pose.y = position.y;
    pose.rotation = object->getRotation();
    pose.scaleX = object->getScaleX();
    pose.scaleY = object->getScaleY();
    EbTriggerWorldStore::writePose(object, pose);
    object->dirtifyObjectRect();
    layer->updateObjectSection(object);
}

std::string EbNativeIsolationProbe::run() {
    auto* manager = GameManager::sharedState();
    if (!manager || manager->m_playLayer || manager->m_gameLayer)
        return "Run Native Isolation from the main menu after leaving the level.";

    EbNativeMemoryGuard memoryGuard;
    auto const autoreleasesBefore = EbNativeMemoryGuard::autoreleaseCount();
    auto* level = GJGameLevel::create();
    level->m_levelName = "EarlyBuffer isolation";
    level->m_levelString = "kA2,0,kA3,0,kA4,0,kA6,0,kA7,0,kA8,0,kA9,0,kA10,0;"
                           "1,1,2,150,3,90;1,1,2,180,3,90;1,1,2,210,3,90;"
                           "1,8,2,300,3,15;1,36,2,90,3,105;";
    auto* savedPlay = manager->m_playLayer;
    auto* savedGame = manager->m_gameLayer;
    EbShadowSimulator firstSim;
    EbShadowSimulator secondSim;
    EbShadowSimulator mainSim;
    firstSim.m_simulating = true;
    secondSim.m_simulating = true;
    s_active = &firstSim;
    s_creating = true;
    auto* first = PlayLayer::create(level, false, false);
    s_active = &secondSim;
    auto* second = PlayLayer::create(level, false, false);
    mainSim.m_simulating = true;
    s_active = &mainSim;
    auto* mainWorld = PlayLayer::create(level, false, false);
    s_creating = false;
    s_active = nullptr;
    manager->m_playLayer = savedPlay;
    manager->m_gameLayer = savedGame;
    if (!first || !second || !mainWorld || !first->m_player1 || !second->m_player1 ||
        !mainWorld->m_player1)
        return "Native isolation failed: could not initialize both fixture worlds.";

    mainWorld->unscheduleAllSelectors();
    mainWorld->stopAllActions();
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
    manager->m_playLayer = mainWorld;
    manager->m_gameLayer = mainWorld;
    s_active = &mainSim;
    mainSim.attach(mainWorld);
    if (!firstSim.m_shadow || !secondSim.m_shadow || !mainSim.m_shadow) {
        firstSim.detach(true);
        secondSim.detach(true);
        mainSim.detach(true);
        s_active = nullptr;
        manager->m_playLayer = savedPlay;
        manager->m_gameLayer = savedGame;
        return "Native isolation failed: fixture ghosts could not initialize.";
    }
    firstSim.m_simulating = true;
    secondSim.m_simulating = true;
    mainSim.m_simulating = true;
    using RunTrace = std::vector<TickResult>;
    std::array<std::vector<RunTrace>, 3> reference;
    std::array<std::vector<RunTrace>, 3> workerTrace;
    for (int world = 0; world < 3; ++world) {
        reference[world].resize(1000);
        workerTrace[world].resize(1000);
        for (int repetition = 0; repetition < 1000; ++repetition) {
            reference[world][repetition].reserve(96);
            workerTrace[world][repetition].reserve(96);
        }
    }
    std::array<PlayLayer*, 3> fixtureWorlds{first, second, mainWorld};
    std::array<GameObject*, 3> movingBlocks{};
    std::array<RingObject*, 3> fixtureOrbs{};
    std::array<cocos2d::CCPoint, 3> blockOrigins{};
    bool fixtureReady = true;
    for (int world = 0; world < 3; ++world) {
        auto* objects = fixtureWorlds[world]->m_objects;
        if (objects) {
            for (unsigned index = 0; index < objects->count(); ++index) {
                auto* object = static_cast<GameObject*>(objects->objectAtIndex(index));
                if (object->m_objectID == 1 && !movingBlocks[world])
                    movingBlocks[world] = object;
                if (object->m_objectID == 36 && !fixtureOrbs[world])
                    fixtureOrbs[world] = geode::prelude::typeinfo_cast<RingObject*>(object);
            }
        }
        if (!movingBlocks[world] || !fixtureOrbs[world]) {
            fixtureReady = false;
            continue;
        }
        // Historical move is part of the initial state before any candidate tick.
        auto* block = movingBlocks[world];
        blockOrigins[world] = block->getPosition() + cocos2d::CCPoint{60.f, 0.f};
        moveFixtureObject(fixtureWorlds[world], block, blockOrigins[world]);
        // Place the ring in the trajectory; only native collision may report a contact.
        moveFixtureObject(fixtureWorlds[world], fixtureOrbs[world],
                          cocos2d::CCPoint{static_cast<float>(world * 30), 105.f});
    }
    if (!fixtureReady) {
        first->cleanup();
        second->cleanup();
        mainWorld->cleanup();
        s_active = &firstSim;
        firstSim.detach(true);
        s_active = &secondSim;
        secondSim.detach(true);
        s_active = &mainSim;
        mainSim.detach(true);
        s_active = nullptr;
        manager->m_playLayer = savedPlay;
        manager->m_gameLayer = savedGame;
        return "INCONCLUSIVE: fixture block or native yellow orb was not loaded.";
    }
    std::atomic<int> activeNative{0};
    std::atomic<int> mainOverlapTicks{0};
    std::atomic<int> overlappingEntries{0};
    auto simulate = [&](PlayLayer* layer, EbShadowSimulator& simulator,
                        std::vector<RunTrace>& traces, int world, bool parallel) {
        s_active = &simulator;
        simulator.m_collectOrbContacts = true;
        simulator.m_savedActivatedIDs = layer->m_gameState.m_activatedObjectIDs;
        for (auto& trace : traces) {
            simulator.resetCaptureForNextRun(layer);
            EbOrbStackResolver::get().begin();
            EbShadowSimulator::tryRemoveFromSection(layer, simulator.m_shadow);
            simulator.copyPlayerState(simulator.m_shadow, layer->m_player1);
            // Distinct starting positions expose cross-world state contamination.
            auto const start = cocos2d::CCPoint{static_cast<float>(world * 30), 105.f};
            simulator.m_shadow->setPosition(start);
            simulator.m_shadow->m_positionX = start.x;
            simulator.m_shadow->m_positionY = start.y;
            simulator.m_shadow->dirtifyObjectRect();
            simulator.m_shadow->m_position = start;
            simulator.m_shadow->m_lastPosition = start;
            simulator.m_shadow->m_yVelocity = 0.;
            simulator.m_shadow->releaseButton(PlayerButton::Jump);
            simulator.m_shadow->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = false;
            simulator.m_ringCount = 0;
            simulator.m_activatedRing = false;
            simulator.m_dead = false;
            simulator.m_deathObject = nullptr;
            for (int tick = 0; tick < 96; ++tick) {
                auto* block = movingBlocks[world];
                // Scripted private motion uses the same absolute fixture tick in both runs.
                float const displacement = static_cast<float>((tick + world * 7) % 32) - 16.f;
                moveFixtureObject(layer, block,
                                  blockOrigins[world] + cocos2d::CCPoint{0.f, displacement});
                // World 1 presses immediately. World 2 waits one collision tick.
                if ((world == 1 && tick == 0) || (world == 2 && tick == 1)) {
                    simulator.m_shadow->pushButton(PlayerButton::Jump);
                    simulator.m_shadow->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] =
                        true;
                }
                if ((world == 1 && tick == 1) || (world == 2 && tick == 2)) {
                    simulator.m_shadow->releaseButton(PlayerButton::Jump);
                    simulator.m_shadow->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] =
                        false;
                }
                if (parallel) {
                    if (world == 2) {
                        if (activeNative.load() > 0)
                            mainOverlapTicks.fetch_add(1);
                    } else if (activeNative.fetch_add(1) > 0) {
                        overlappingEntries.fetch_add(1);
                    }
                }
                simulator.stepPhysics(layer, simulator.m_shadow, 1.f / 240.f, 1.f / 240.f);
                if (parallel && world < 2)
                    activeNative.fetch_sub(1);
                auto* player = simulator.m_shadow;
                TickResult result;
                result.x = player->getPositionX();
                result.y = player->getPositionY();
                result.velocity = player->m_yVelocity;
                result.grounded = player->m_isOnGround;
                result.upsideDown = player->m_isUpsideDown;
                result.dead = simulator.m_dead;
                result.ringCount = simulator.m_ringCount;
                result.jumpHeld =
                    simulator.m_shadow->m_holdingButtons[static_cast<int>(PlayerButton::Jump)];
                result.orbContact =
                    std::find(simulator.m_orbContacts.begin(), simulator.m_orbContacts.end(),
                              fixtureOrbs[world]) != simulator.m_orbContacts.end();
                result.orbOverlap =
                    player->getObjectRect().intersectsRect(fixtureOrbs[world]->getObjectRect());
                result.movingX = block->getPositionX();
                result.movingY = block->getPositionY();
                if (auto* object = simulator.m_deathObject) {
                    result.objectId = object->m_objectID;
                    result.objectX = object->getPositionX();
                    result.objectY = object->getPositionY();
                }
                trace.push_back(result);
                if (result.dead)
                    break;
            }
        }
        simulator.resetCaptureForNextRun(layer);
        simulator.m_collectOrbContacts = false;
        simulator.m_orbContacts.clear();
        moveFixtureObject(layer, movingBlocks[world], blockOrigins[world]);
        s_active = nullptr;
    };
    auto const mainThread = std::this_thread::get_id();
    auto const referenceStart = std::chrono::steady_clock::now();
    manager->m_playLayer = first;
    manager->m_gameLayer = first;
    simulate(first, firstSim, reference[0], 0, false);
    manager->m_playLayer = second;
    manager->m_gameLayer = second;
    simulate(second, secondSim, reference[1], 1, false);
    manager->m_playLayer = mainWorld;
    manager->m_gameLayer = mainWorld;
    simulate(mainWorld, mainSim, reference[2], 2, false);
    auto const referenceEnd = std::chrono::steady_clock::now();

    // Both workers receive a private simulator. Neither can switch native globals.
    // The third world advances on the main thread while native workers run.
    manager->m_playLayer = mainWorld;
    manager->m_gameLayer = mainWorld;

    std::array<bool, 2> ranOnWorker{};
    std::array<std::thread::id, 2> workerIds{};
    std::array<std::string, 3> workerErrors;
    std::array<std::thread, 2> workers;
    std::array<PlayLayer*, 2> layers{first, second};
    std::array<EbShadowSimulator*, 2> simulators{&firstSim, &secondSim};
    std::latch startGate{1};
    std::latch readyGate{2};
    bool released = false;
    auto const workerStart = std::chrono::steady_clock::now();
    try {
        for (int world = 0; world < 2; ++world) {
            workers[world] = std::thread([&, world] {
                workerIds[world] = std::this_thread::get_id();
                ranOnWorker[world] = workerIds[world] != mainThread;
                readyGate.count_down();
                startGate.wait();
                try {
                    simulate(layers[world], *simulators[world], workerTrace[world], world, true);
                } catch (std::exception const& error) {
                    workerErrors[world] = error.what();
                    s_active = nullptr;
                } catch (...) {
                    workerErrors[world] = "Unhandled native worker exception";
                    s_active = nullptr;
                }
            });
        }
        readyGate.wait();
        startGate.count_down();
        released = true;
    } catch (std::exception const& error) {
        workerErrors[0] = error.what();
    }
    if (!released)
        startGate.count_down();
    // This is native main-thread physics, with no per-tick waits on either worker.
    try {
        simulate(mainWorld, mainSim, workerTrace[2], 2, true);
    } catch (std::exception const& error) {
        workerErrors[2] = error.what();
        s_active = nullptr;
    } catch (...) {
        workerErrors[2] = "Unhandled native main-thread exception";
        s_active = nullptr;
    }
    for (auto& worker : workers) {
        if (worker.joinable())
            worker.join();
    }
    auto const workerEnd = std::chrono::steady_clock::now();
    bool const worldPointersUnchanged =
        manager->m_playLayer == mainWorld && manager->m_gameLayer == mainWorld;
    int differences = 0;
    int collisions = 0;
    int survivals = 0;
    int verifiedOrbRuns = 0;
    int detectedOrbRuns = 0;
    int delayedOrbRuns = 0;
    std::array<int, 3> referenceDeaths{};
    std::array<int, 3> comparedRuns{};
    int firstDifference = -1;
    int firstRepetition = -1;
    int firstWorld = -1;
    size_t referenceTicks = 0;
    for (int world = 0; world < 3; ++world) {
        for (int repetition = 0; repetition < 1000; ++repetition) {
            auto const& left = reference[world][repetition];
            auto const& right = workerTrace[world][repetition];
            referenceTicks += left.size();
            if (!left.empty() && left.back().dead)
                ++referenceDeaths[world];
            int divergentTick = -1;
            size_t const common = std::min(left.size(), right.size());
            for (size_t tick = 0; tick < common; ++tick) {
                if (!left[tick].matches(right[tick])) {
                    divergentTick = static_cast<int>(tick);
                    break;
                }
            }
            if (divergentTick < 0 && left.size() != right.size())
                divergentTick = static_cast<int>(common);
            if (divergentTick >= 0 || left.empty()) {
                ++differences;
                if (firstWorld < 0) {
                    firstWorld = world;
                    firstRepetition = repetition;
                    firstDifference = divergentTick;
                }
            } else {
                ++comparedRuns[world];
                if (world != 0 && left.back().ringCount == 1)
                    ++verifiedOrbRuns;
                bool const detected =
                    std::any_of(left.begin(), left.end(),
                                [](TickResult const& tick) { return tick.orbContact; });
                if (world != 0 && detected)
                    ++detectedOrbRuns;
                if (world == 2 && left.size() > 1 && left[0].orbOverlap && !left[0].jumpHeld &&
                    left[0].ringCount == 0 && left.back().ringCount > 0)
                    ++delayedOrbRuns;
                if (left.back().dead)
                    ++collisions;
                else if (left.size() == 96)
                    ++survivals;
            }
        }
    }
    mainWorld->cleanup();
    first->cleanup();
    second->cleanup();
    firstSim.m_simulating = secondSim.m_simulating = mainSim.m_simulating = false;
    s_active = &firstSim;
    firstSim.detach(true);
    s_active = &secondSim;
    secondSim.detach(true);
    s_active = &mainSim;
    mainSim.detach(true);
    s_active = nullptr;
    manager->m_playLayer = savedPlay;
    manager->m_gameLayer = savedGame;
    std::ostringstream report;
    report << "AP-P03 native orb collision + delayed press" << std::endl
           << "Distinct worker threads: "
           << ((ranOnWorker[0] && ranOnWorker[1] && workerIds[0] != workerIds[1]) ? "yes" : "no")
           << std::endl
           << "Overlapping native-call entries: " << overlappingEntries.load() << std::endl
           << "Main-thread ticks overlapping workers: " << mainOverlapTicks.load() << std::endl
           << "Differences: " << differences << std::endl
           << "First divergent world / run / tick: " << firstWorld << " / " << firstRepetition
           << " / " << firstDifference << std::endl
           << "Matching deaths / full-horizon survivals: " << collisions << " / " << survivals
           << std::endl
           << "Matching activated-orb runs: " << verifiedOrbRuns << std::endl
           << "Matching native-contact runs: " << detectedOrbRuns << std::endl
           << "Matching delayed-press runs: " << delayedOrbRuns << std::endl
           << "Reference physics ticks: " << referenceTicks << std::endl
           << "Global world pointers unchanged: " << (worldPointersUnchanged ? "yes" : "no")
           << std::endl
           << "Serial ms (three worlds): "
           << std::chrono::duration<double, std::milli>(referenceEnd - referenceStart).count()
           << std::endl
           << "Concurrent ms (includes main physics + launch/join): "
           << std::chrono::duration<double, std::milli>(workerEnd - workerStart).count()
           << std::endl;
    for (int world = 0; world < 3; ++world) {
        if (!workerErrors[world].empty())
            report << "Worker " << world << " error: " << workerErrors[world] << std::endl;
    }
    for (int world = 0; world < 3; ++world) {
        report << "World " << world
               << " reference deaths / matched runs: " << referenceDeaths[world] << " / "
               << comparedRuns[world] << std::endl;
        auto const& trace = reference[world][0];
        int firstContact = -1;
        int firstActivation = -1;
        for (size_t tick = 0; tick < trace.size(); ++tick) {
            if (trace[tick].orbContact && firstContact < 0)
                firstContact = static_cast<int>(tick);
            if (trace[tick].ringCount > 0 && firstActivation < 0)
                firstActivation = static_cast<int>(tick);
        }
        report << "  First contact / activation tick: " << firstContact << " / " << firstActivation
               << std::endl;
    }
    bool const passed =
        differences == 0 && collisions + survivals == 3000 && verifiedOrbRuns == 2000 &&
        detectedOrbRuns == 2000 && delayedOrbRuns == 1000 && referenceTicks > 3000 &&
        overlappingEntries.load() > 0 && mainOverlapTicks.load() > 0 && worldPointersUnchanged &&
        ranOnWorker[0] && ranOnWorker[1] && workerIds[0] != workerIds[1] &&
        workerErrors[0].empty() && workerErrors[1].empty() && workerErrors[2].empty();
    report << (passed ? "Concurrent native-orb-contact fixture parity passed."
                      : "INCONCLUSIVE / FAILED: concurrent fixture criteria unmet.")
           << std::endl
           << "Main thread simulated a third native fixture during worker execution." << std::endl
           << "Motion is scripted; contact comes from native collision. Trigger replay, "
              "rendering and live attempts remain unverified.";
    auto compactReport = report.str();
    for (int world = 0; world < 3; ++world) {
        auto* ring = fixtureOrbs[world];
        auto const rect = ring->getObjectRect();
        report << std::endl
               << "Orb world=" << world << " node=" << ring->getPositionX() << ","
               << ring->getPositionY() << " native=" << ring->m_positionX << ","
               << ring->m_positionY << " rect=" << rect.origin.x << "," << rect.origin.y << ","
               << rect.size.width << "," << rect.size.height;
    }
    report << std::endl << "First-run tick evidence (reference / concurrent):" << std::endl;
    for (int world = 0; world < 3; ++world) {
        for (int execution = 0; execution < 2; ++execution) {
            auto const& trace = execution == 0 ? reference[world][0] : workerTrace[world][0];
            for (size_t tick = 0; tick < trace.size(); ++tick) {
                auto const& result = trace[tick];
                report << "world=" << world << " concurrent=" << execution << " tick=" << tick
                       << " pos=" << result.x << "," << result.y << " vy=" << result.velocity
                       << " held=" << result.jumpHeld << " overlap=" << result.orbOverlap
                       << " contact=" << result.orbContact << " rings=" << result.ringCount
                       << " dead=" << result.dead << " deathOid=" << result.objectId << std::endl;
            }
        }
    }
    // All worker threads have joined and all Cocos evidence has been copied.
    // Drain the diagnostic pool now, rather than leaving it to the frame pool.
    memoryGuard.drain();
    auto const guardedAutoreleases = EbNativeMemoryGuard::autoreleaseCount() - autoreleasesBefore;
    report << std::endl
           << "Scoped Cocos pool drained: yes" << std::endl
           << "Guarded autorelease calls: " << guardedAutoreleases;
    compactReport += "\nScoped Cocos pool drained: yes";
    compactReport += "\nGuarded autorelease calls: " + std::to_string(guardedAutoreleases);
    auto const path = geode::Mod::get()->getSaveDir() / "native-isolation.txt";
    auto const written = geode::utils::file::writeString(path, report.str());
    if (written.isErr())
        return compactReport + "\nCould not save report.";
    else
        return compactReport + "\nSaved: " + path.string();
}
