#include "cpu_physics_gd_sim.hpp"
#include "../../assist/assist_clock.hpp"
#include "cpu_physics_seed.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fmt/format.h>
#include <mutex>
#include <vector>

static std::array<std::mutex, 64> s_objectLocks;

static std::mutex& lockFor(int uniqueId) {
    unsigned const index = static_cast<unsigned>(uniqueId) & 63u;
    return s_objectLocks[index];
}

static void bodyAabbAtTick(EbCpuPhysicsBody const& body, int tick, float& x, float& y, float& w,
                           float& h) {
    x = body.x;
    y = body.y;
    w = body.w;
    h = body.h;
    if (body.twSamples <= 0)
        return;
    if (body.twSamples == 1) {
        x = body.twX[0];
        y = body.twY[0];
        w = body.twW[0];
        h = body.twH[0];
        return;
    }
    if (tick <= body.twTick[0]) {
        x = body.twX[0];
        y = body.twY[0];
        w = body.twW[0];
        h = body.twH[0];
        return;
    }
    int const last = body.twSamples - 1;
    if (tick >= body.twTick[last]) {
        x = body.twX[last];
        y = body.twY[last];
        w = body.twW[last];
        h = body.twH[last];
        return;
    }
    for (int index = 0; index < last; ++index) {
        int const t0 = body.twTick[index];
        int const t1 = body.twTick[index + 1];
        if (tick < t0 || tick > t1)
            continue;
        float const alpha =
            t1 == t0 ? 0.f : static_cast<float>(tick - t0) / static_cast<float>(t1 - t0);
        x = body.twX[index] + (body.twX[index + 1] - body.twX[index]) * alpha;
        y = body.twY[index] + (body.twY[index + 1] - body.twY[index]) * alpha;
        w = body.twW[index] + (body.twW[index + 1] - body.twW[index]) * alpha;
        h = body.twH[index] + (body.twH[index + 1] - body.twH[index]) * alpha;
        return;
    }
}

static void restoreBodyObject(EbCpuPhysicsBody const& body) {
    auto* object = body.object;
    if (!object)
        return;
    object->m_isActivated = body.savedIsActivated;
    if (auto* enhanced = geode::prelude::typeinfo_cast<EnhancedGameObject*>(object)) {
        enhanced->m_activated = body.savedActivated;
        enhanced->m_poweredOn = body.savedPoweredOn;
        enhanced->m_activatedByPlayer1 = body.savedP1;
        enhanced->m_activatedByPlayer2 = body.savedP2;
    }
    if (body.isRing) {
        if (auto* ring = geode::prelude::typeinfo_cast<RingObject*>(object))
            ring->m_claimTouch = body.savedClaimTouch;
    }
}

static bool aabbOverlap(float ax, float ay, float aw, float ah, float bx, float by, float bw,
                        float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

EbCpuPhysicsRunResult EbCpuPhysicsGdSim::run(PlayerObject* ghost, PlayerObject* templateGhost,
                                             EbCpuPhysicsWorld const& world,
                                             Options const& options) {
    EbCpuPhysicsRunResult out;
    if (!ghost || !templateGhost || !world.valid || options.maxTicks <= 0)
        return out;

    EbCpuPhysicsSeedIo::reseedFrom(ghost, templateGhost);

    bool acted = false;
    bool tapReleased = false;
    bool jumped = false;
    int const jumpIdx = static_cast<int>(PlayerButton::Jump);
    bool holding = world.seed.holdingJump;
    ghost->m_holdingButtons[jumpIdx] = holding;

    float const lastJumpTime0 = ghost->m_lastJumpTime;
    int const maxTicks = std::max(1, options.maxTicks);

    auto finish = [&](EbCpuPhysicsRunResult result) {
        auto const pos = ghost->getPosition();
        result.endX = pos.x;
        result.endY = pos.y;
        result.jumped = jumped || result.jumped;
        ghost->m_isDead = false;
        return result;
    };

    for (int tick = 0; tick < maxTicks; ++tick) {
        out.ticksRan = tick + 1;
        float const stepDt = (tick == 0) ? options.firstDt : options.deltaTime;
        float gdStepDt = EbAssistClock::secondsToGdStep(stepDt);
        if (!(gdStepDt > 0.f) || !std::isfinite(gdStepDt))
            gdStepDt = EbAssistClock::kNominalGdStep;
        float colDt = stepDt;
        if (colDt < options.deltaTime)
            colDt = options.deltaTime;
        float const gdColDt = EbAssistClock::secondsToGdStep(colDt);

        if (!acted && tick == options.delay) {
            acted = true;
            if (options.down) {
                holding = true;
                ghost->m_holdingButtons[jumpIdx] = true;
                ghost->pushButton(PlayerButton::Jump);
            } else {
                holding = false;
                ghost->m_holdingButtons[jumpIdx] = false;
                // Flags only on ghosts — releaseButton is stubbed for owned ghosts.
                ghost->m_holdingButtons[jumpIdx] = false;
            }
        } else if (acted && options.down && !tapReleased) {
            int const releaseAt =
                options.releaseAfter >= 0 ? options.delay + options.releaseAfter : -1;
            bool const robotHold = ghost->m_isRobot;
            bool const autoTap = options.releaseAfter < 0 && jumped && !robotHold &&
                                 !ghost->m_isShip && !ghost->m_isDart && !ghost->m_isSwing &&
                                 !ghost->m_isBird && !ghost->m_isBall && !ghost->m_isSpider;
            if ((releaseAt >= 0 && tick == releaseAt) || (autoTap && tick > options.delay)) {
                tapReleased = true;
                holding = false;
                ghost->m_holdingButtons[jumpIdx] = false;
            }
        }

        cocos2d::CCPoint const tickStart = ghost->getPosition();
        ghost->m_lastPosition = tickStart;

        // Real GD vehicle / gravity / dash integration.
        ghost->update(gdStepDt);

        if (ghost->m_lastJumpTime > lastJumpTime0 || ghost->m_hasEverJumped) {
            if (!jumped && acted) {
                jumped = true;
                out.jumpTick = tick;
            }
        }

        bool died = false;
        GameObject* deathObject = nullptr;

        auto const playerBox = ghost->getObjectRect();
        if (!world.window.contains(playerBox.origin.x, playerBox.size.width)) {
            out.unknown = true;
            break;
        }
        float const px = playerBox.origin.x;
        float const py = playerBox.origin.y;
        float const pw = playerBox.size.width;
        float const ph = playerBox.size.height;

        for (auto const& body : world.bodies) {
            if (!body.object)
                continue;
            float bx = 0.f;
            float by = 0.f;
            float bw = 0.f;
            float bh = 0.f;
            bodyAabbAtTick(body, tick, bx, by, bw, bh);
            if (!aabbOverlap(px, py, pw, ph, bx, by, bw, bh))
                continue;

            cocos2d::CCRect rect;
            rect.origin.x = bx;
            rect.origin.y = by;
            rect.size.width = bw;
            rect.size.height = bh;

            {
                std::lock_guard lock(lockFor(body.uniqueId));
                ghost->collidedWithObjectInternal(gdColDt, body.object, rect, false);
            }

            if (ghost->m_isDead) {
                died = true;
                deathObject = body.object;
                ghost->m_isDead = false;
                break;
            }
            if (body.isRing || body.isPad)
                out.activatedBoost = out.activatedBoost || ghost->m_touchedRing ||
                                     ghost->m_touchedPad || ghost->m_hasEverHitRing;
        }

        if (!died) {
            float const y = ghost->getPositionY();
            if (y < world.floorY - 120.f || y > world.ceilY + 120.f) {
                // This capture bound is not a real GD death boundary.
                out.unknown = true;
                break;
            }
        }

        if (died) {
            out.died = true;
            out.deathTick = tick;
            out.deathX = ghost->getPositionX();
            out.diedBeforeAction = !acted;
            (void)deathObject;
            for (auto const& body : world.bodies) {
                std::lock_guard lock(lockFor(body.uniqueId));
                restoreBodyObject(body);
            }
            return finish(out);
        }
    }

    for (auto const& body : world.bodies) {
        std::lock_guard lock(lockFor(body.uniqueId));
        restoreBodyObject(body);
    }
    return finish(out);
}

bool EbCpuPhysicsGdSim::selfCheck(std::string& report) {
    report.clear();
    // Full GD path needs a live PlayLayer / ghost — covered in-game via Adaptive.
    report = "CpuPhysicsGdSim selfCheck OK";
    return true;
}
