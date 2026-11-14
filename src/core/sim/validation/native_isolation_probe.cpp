#include "native_isolation_probe.hpp"
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
           objectId == other.objectId && objectX == other.objectX && objectY == other.objectY;
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
    std::atomic<int> activeNative{0};
    std::atomic<int> mainOverlapTicks{0};
    std::atomic<int> overlappingEntries{0};
    auto simulate = [&](PlayLayer* layer, EbShadowSimulator& simulator,
                        std::vector<RunTrace>& traces, int world, bool parallel) {
        s_active = &simulator;
        for (auto& trace : traces) {
            EbShadowSimulator::tryRemoveFromSection(layer, simulator.m_shadow);
            simulator.copyPlayerState(simulator.m_shadow, layer->m_player1);
            // Distinct starting positions expose cross-world state contamination.
            auto const start = cocos2d::CCPoint{static_cast<float>(world * 30), 105.f};
            simulator.m_shadow->setPosition(start);
            simulator.m_shadow->m_position = start;
            simulator.m_shadow->m_lastPosition = start;
            simulator.m_shadow->m_yVelocity = 0.;
            simulator.m_dead = false;
            simulator.m_deathObject = nullptr;
            for (int tick = 0; tick < 96; ++tick) {
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
    report << "AP-P03 two workers + main-thread physics, 1000 runs per world" << std::endl
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
    for (int world = 0; world < 3; ++world)
        report << "World " << world
               << " reference deaths / matched runs: " << referenceDeaths[world] << " / "
               << comparedRuns[world] << std::endl;
    bool const passed = differences == 0 && collisions + survivals == 3000 &&
                        referenceTicks > 3000 && overlappingEntries.load() > 0 &&
                        mainOverlapTicks.load() > 0 && worldPointersUnchanged && ranOnWorker[0] &&
                        ranOnWorker[1] && workerIds[0] != workerIds[1] && workerErrors[0].empty() &&
                        workerErrors[1].empty() && workerErrors[2].empty();
    report << (passed ? "Concurrent static fixture parity passed."
                      : "INCONCLUSIVE / FAILED: concurrent fixture criteria unmet.")
           << std::endl
           << "Main thread simulated a third native fixture during worker execution." << std::endl
           << "Rendering, live attempts, dynamic objects and other modes remain unverified.";
    auto const path = geode::Mod::get()->getSaveDir() / "native-isolation.txt";
    auto const written = geode::utils::file::writeString(path, report.str());
    if (written.isErr())
        report << "\nCould not save report.";
    else
        report << "\nSaved: " << path.string();
    return report.str();
}
