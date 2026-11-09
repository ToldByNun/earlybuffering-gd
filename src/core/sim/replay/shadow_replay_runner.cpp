#include "shadow_replay_runner.hpp"
#include "../../../config/stage_config.hpp"
#include "../../replay/replay_import.hpp"
#include "../planning/simulation_plan.hpp"
#include "../shadow_simulator.hpp"
#include "../sim_session_guard.hpp"
#include <algorithm>
#include <vector>

EbShadowSimulator::ReplayResult EbShadowReplayRunner::replay(EbShadowSimulator& simulator,
                                                         PlayLayer* layer, int slot, int startTick,
                                                         std::vector<EbInputEvent> const& timeline,
                                                         float deltaTime, int maxTicks,
                                                         bool shiftActionBy, int actionShift) {
    EbShadowSimulator::ReplayResult out;
    auto* snap = simulator.snapshotSlot(slot);
    if (!layer || !snap || !simulator.m_shadow || deltaTime <= 0.f || maxTicks <= 0)
        return out;

    simulator.copyPlayerState(simulator.m_shadow, snap);
    simulator.m_dead = false;
    simulator.m_jumped = false;
    simulator.m_unknown = false;
    simulator.m_activatedRing = false;
    simulator.m_ringCount = 0;
    simulator.m_deathObject = nullptr;

    auto* shadowPlayer = simulator.m_shadow;
    // Build a shifted timeline view: each event tick += actionShift if requested.
    auto effectiveTick = [&](EbInputEvent const& ev) {
        if (!shiftActionBy)
            return ev.tick;
        return ev.tick + actionShift;
    };

    size_t nextEv = 0;
    // Apply any events at or before startTick immediately.
    while (nextEv < timeline.size() && effectiveTick(timeline[nextEv]) <= startTick) {
        if (timeline[nextEv].down)
            shadowPlayer->pushButton(PlayerButton::Jump);
        else
            shadowPlayer->releaseButton(PlayerButton::Jump);
        ++nextEv;
    }

    simulator.m_simTickBase = std::max(0, startTick);
    simulator.m_simAbsTick = -1;
    {
        EbSimSessionGuard session(*(&simulator), layer);

        for (int tick = 0; tick < maxTicks; ++tick) {
            int const absTick = startTick + tick;
            simulator.m_simAbsTick = absTick;

            while (nextEv < timeline.size() && effectiveTick(timeline[nextEv]) == absTick) {
                if (timeline[nextEv].down)
                    shadowPlayer->pushButton(PlayerButton::Jump);
                else
                    shadowPlayer->releaseButton(PlayerButton::Jump);
                ++nextEv;
            }

            simulator.stepPhysics(layer, shadowPlayer, deltaTime, deltaTime);
            out.endX = shadowPlayer->getPositionX();
            if (simulator.m_dead) {
                out.died = true;
                out.deathTick = tick;
                out.deathObject = simulator.m_deathObject;
                break;
            }
            if (simulator.m_unknown) {
                out.unknown = true;
                break;
            }
        }

        out.activatedRing = simulator.m_activatedRing;
        out.ringCount = simulator.m_ringCount;
    }
    return out;
}

EbShadowSimulator::BakeRunResult EbShadowReplayRunner::runBakeTimeline(
    EbShadowSimulator& simulator, PlayLayer* layer, int slot, std::vector<EbReplayEvent> const& events,
    float deltaTime, int untilFrame, int sampleFrame, bool applyP2ToShadow, bool traceFrames,
    bool parkReals, int substeps, std::function<bool(int frame)> onFrame) {
    EbShadowSimulator::BakeRunResult out;
    auto* snap = simulator.snapshotSlot(slot);
    if (!layer || !snap || !simulator.m_shadow || deltaTime <= 0.f || untilFrame < 0)
        return out;
    if (substeps < 1)
        substeps = 1;
    if (substeps > 32)
        substeps = 32;
    float const subDt = deltaTime / static_cast<float>(substeps);

    // Live players share spawn with the ghost — any residual collision path
    // (not only collisionCheckObjects) can mark frame-0 death. Park them.
    cocos2d::CCPoint savedP1{};
    cocos2d::CCPoint savedP2{};
    bool parkedP1 = false;
    bool parkedP2 = false;
    if (parkReals) {
        if (layer->m_player1 && layer->m_player1 != simulator.m_shadow) {
            savedP1 = layer->m_player1->getPosition();
            layer->m_player1->setPosition({-100000.f, -100000.f});
            parkedP1 = true;
        }
        if (layer->m_player2 && layer->m_player2 != simulator.m_shadow) {
            savedP2 = layer->m_player2->getPosition();
            layer->m_player2->setPosition({-100000.f, -100000.f});
            parkedP2 = true;
        }
    }

    simulator.copyPlayerState(simulator.m_shadow, snap);
    simulator.m_dead = false;
    simulator.m_jumped = false;
    simulator.m_unknown = false;
    simulator.m_toggleUnknownHit = false;
    simulator.m_deathObject = nullptr;
    simulator.m_unknownHistMap.clear();
    simulator.m_collectUnknownHist = true;

    auto* shadowPlayer = simulator.m_shadow;
    size_t nextEv = 0;
    if (traceFrames) {
        out.frames.reserve(static_cast<size_t>(std::min(untilFrame, 2048)) + 2);
    }

    auto applyEv = [&](EbReplayEvent const& ev) {
        if (ev.player2 && !applyP2ToShadow)
            return;
        auto buttonValue = PlayerButton::Jump;
        if (ev.button == 2)
            buttonValue = PlayerButton::Left;
        else if (ev.button == 3)
            buttonValue = PlayerButton::Right;
        if (ev.down)
            shadowPlayer->pushButton(buttonValue);
        else
            shadowPlayer->releaseButton(buttonValue);
    };

    simulator.m_simTickBase = 0;
    simulator.m_simAbsTick = -1;
    simulator.m_poseRemapDt = deltaTime; // TW remap must match bake physics, not live hitch dt
    simulator.m_bakeOffline = true;
    {
        EbSimSessionGuard session(*(&simulator), layer);

        for (int frame = 0; frame <= untilFrame; ++frame) {
            simulator.m_simAbsTick = frame;

            // Clickbots often put press+release on the SAME frame. Apply downs
            // BEFORE physics so the tick is held; releases AFTER the step.
            std::vector<EbReplayEvent const*> downs;
            std::vector<EbReplayEvent const*> ups;
            while (nextEv < events.size() && events[nextEv].frame == frame) {
                auto const& ev = events[nextEv];
                if (!ev.player2 || applyP2ToShadow) {
                    if (ev.down)
                        downs.push_back(&ev);
                    else
                        ups.push_back(&ev);
                }
                ++nextEv;
            }
            for (auto const* event : downs)
                applyEv(*event);
            bool const jumpHeldThisStep =
                shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)];

            if (sampleFrame >= 0 && frame == sampleFrame) {
                out.xAtSample = shadowPlayer->getPositionX();
                out.modeAtSample = EbStageConfig::kindOf(shadowPlayer);
                out.dashingAtSample = shadowPlayer->m_isDashing;
                out.sampled = true;
            }

            // Physics-bypass: N micro-steps at author dt inside one bake tick.
            for (int sub = 0; sub < substeps; ++sub) {
                simulator.stepPhysics(layer, shadowPlayer, subDt, subDt);
                if (simulator.m_dead)
                    break;
            }
            for (auto const* event : ups)
                applyEv(*event);

            // Hard death only — Unknown must not wipe a working replay bake
            // (toggle-group solids used to abort every click via m_unknown).
            if (simulator.m_unknown) {
                out.unknown = true;
            }

            if (onFrame && !onFrame(frame)) {
                out.died = simulator.m_dead;
                out.deathFrame = frame;
                out.deathX = shadowPlayer->getPositionX();
                out.deathY = shadowPlayer->getPositionY();
                break;
            }

            int const evThis = static_cast<int>(downs.size() + ups.size());
            if (traceFrames) {
                EbShadowSimulator::BakeFrameTrace tr;
                tr.frame = frame;
                tr.x = shadowPlayer->getPositionX();
                tr.y = shadowPlayer->getPositionY();
                tr.yVel = static_cast<float>(shadowPlayer->m_yVelocity);
                tr.jumpHeld = jumpHeldThisStep;
                tr.dead = simulator.m_dead;
                tr.unknown = simulator.m_unknown;
                tr.twApplied = simulator.m_triggerWorldAppliedThisTick;
                tr.eventsApplied = evThis;
                tr.mode = static_cast<int>(EbStageConfig::kindOf(shadowPlayer));
                if (simulator.m_dead && simulator.m_deathObject) {
                    tr.deathObjId = simulator.m_deathObject->m_uniqueID;
                    tr.deathObjType = static_cast<int>(simulator.m_deathObject->m_objectType);
                    tr.deathObjX = simulator.m_deathObject->getPositionX();
                    tr.deathObjY = simulator.m_deathObject->getPositionY();
                }
                out.frames.push_back(tr);
            }

            if (simulator.m_dead) {
                out.died = true;
                out.deathFrame = frame;
                out.deathX = shadowPlayer->getPositionX();
                out.deathY = shadowPlayer->getPositionY();
                if (simulator.m_deathObject) {
                    out.deathObjId = simulator.m_deathObject->m_uniqueID;
                    out.deathObjType = static_cast<int>(simulator.m_deathObject->m_objectType);
                    out.deathObjGameId = simulator.m_deathObject->m_objectID;
                }
                break;
            }
        }
    }

    simulator.m_collectUnknownHist = false;
    simulator.m_bakeOffline = false;
    // Seal Soft/Hard unknown histogram (count desc).
    out.unknownHist.clear();
    out.unknownHist.reserve(simulator.m_unknownHistMap.size());
    out.unknownSoftHits = 0;
    out.unknownHardHits = 0;
    for (auto const& kv : simulator.m_unknownHistMap) {
        out.unknownHist.push_back(kv.second);
        if (kv.second.hard)
            out.unknownHardHits += kv.second.count;
        else
            out.unknownSoftHits += kv.second.count;
    }
    std::sort(
        out.unknownHist.begin(), out.unknownHist.end(),
        [](EbShadowSimulator::UnknownHistBin const& a, EbShadowSimulator::UnknownHistBin const& b) {
            if (a.count != b.count)
                return a.count > b.count;
            return a.objectId < b.objectId;
        });
    simulator.m_unknownHistMap.clear();

    if (parkedP1 && layer->m_player1) {
        layer->m_player1->setPosition(savedP1);
    }
    if (parkedP2 && layer->m_player2) {
        layer->m_player2->setPosition(savedP2);
    }
    return out;
}

EbShadowSimulator::PathSample EbShadowReplayRunner::samplePath(EbShadowSimulator& simulator,
                                                           PlayLayer* layer, int slot,
                                                           std::vector<EbReplayEvent> const& events,
                                                           float deltaTime, int untilFrame) {
    EbShadowSimulator::PathSample out;
    auto* snap = simulator.snapshotSlot(slot);
    if (!layer || !snap || !simulator.m_shadow || deltaTime <= 0.f || untilFrame < 0)
        return out;

    simulator.copyPlayerState(simulator.m_shadow, snap);
    simulator.m_dead = false;
    simulator.m_jumped = false;
    simulator.m_unknown = false;
    simulator.m_deathObject = nullptr;

    auto* shadowPlayer = simulator.m_shadow;
    size_t nextEv = 0;
    out.points.reserve(static_cast<size_t>(untilFrame) + 2);

    auto applyEv = [&](EbReplayEvent const& ev) {
        auto buttonValue = PlayerButton::Jump;
        if (ev.button == 2)
            buttonValue = PlayerButton::Left;
        else if (ev.button == 3)
            buttonValue = PlayerButton::Right;
        if (ev.down)
            shadowPlayer->pushButton(buttonValue);
        else
            shadowPlayer->releaseButton(buttonValue);
    };

    simulator.m_simTickBase = 0;
    simulator.m_simAbsTick = -1;
    simulator.m_poseRemapDt = deltaTime;
    {
        EbSimSessionGuard session(*(&simulator), layer);

        out.points.push_back(shadowPlayer->getPosition());
        for (int frame = 0; frame <= untilFrame; ++frame) {
            simulator.m_simAbsTick = frame;
            while (nextEv < events.size() && events[nextEv].frame == frame) {
                applyEv(events[nextEv]);
                ++nextEv;
            }
            simulator.stepPhysics(layer, shadowPlayer, deltaTime, deltaTime);
            out.points.push_back(shadowPlayer->getPosition());
            if (simulator.m_dead || simulator.m_unknown) {
                out.died = true;
                out.deathFrame = frame;
                break;
            }
        }
    }
    return out;
}
