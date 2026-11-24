#include "cpu_physics_sim.hpp"
#include "../../assist/assist_clock.hpp"
#include "../foresight/travel_foresight.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

static float boostImpulse(std::uint8_t boost) {
    switch (boost) {
    case 0:
        return 11.18f; // yellow
    case 1:
        return 13.5f; // pink
    case 2:
        return 16.5f; // red
    case 3:
        return 11.18f; // gravity flip uses same magnitude
    default:
        return 12.0f;
    }
}

static bool aabbOverlap(float ax, float ay, float aw, float ah, float bx, float by, float bw,
                        float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

EbCpuPhysicsRunResult EbCpuPhysicsSim::run(EbCpuPhysicsWorld const& world, Options const& options) {
    EbCpuPhysicsRunResult out;
    if (!world.valid || options.maxTicks <= 0)
        return out;

    EbCpuPhysicsPlayer p = world.player;
    bool holding = p.holdingJump;
    bool acted = false;
    bool tapReleased = false;
    bool jumped = false;
    int gravSign = p.upsideDown ? -1 : 1;

    float const pxPerTick1x = EbTravelForesight::kBasePxPerTickAt1x;
    int const maxTicks = std::max(1, options.maxTicks);

    auto finish = [&](EbCpuPhysicsRunResult result) {
        result.endX = p.x + p.w * 0.5f;
        result.endY = p.y + p.h * 0.5f;
        result.jumped = jumped || result.jumped;
        return result;
    };

    for (int tick = 0; tick < maxTicks; ++tick) {
        out.ticksRan = tick + 1;
        float const stepDt = (tick == 0) ? options.firstDt : options.deltaTime;
        float gdDt = EbAssistClock::secondsToGdStep(stepDt);
        if (!(gdDt > 0.f) || !std::isfinite(gdDt))
            gdDt = EbAssistClock::kNominalGdStep;
        float const tickScale = gdDt / EbAssistClock::kNominalGdStep;

        if (!acted && tick == options.delay) {
            acted = true;
            if (options.down) {
                holding = true;
                // Discrete jump from ground / ball flip impulse.
                if (!p.continuous) {
                    if (p.grounded || p.mode == 2 /*Ball*/ || p.mode == 6 /*Spider*/) {
                        p.yVel = boostImpulse(0) * static_cast<float>(-gravSign);
                        p.grounded = false;
                        jumped = true;
                        out.jumpTick = tick;
                        if (p.mode == 2 || p.mode == 6)
                            gravSign = -gravSign;
                    }
                }
            } else {
                holding = false;
            }
        } else if (acted && options.down && !tapReleased) {
            int const releaseAt =
                options.releaseAfter >= 0 ? options.delay + options.releaseAfter : -1;
            bool const autoTap = options.releaseAfter < 0 && p.tapRelease && jumped;
            if ((releaseAt >= 0 && tick == releaseAt) || (autoTap && tick > options.delay)) {
                tapReleased = true;
                holding = false;
            }
        }

        // Horizontal travel (matches foresight 1x px/tick scaling).
        p.x += p.playerSpeed * pxPerTick1x * tickScale;

        // Vertical / vehicle control.
        if (p.mode == 4) { // Wave
            float const waveSpeed = 11.18f * p.playerSpeed;
            p.yVel = (holding ? -1.f : 1.f) * waveSpeed * static_cast<float>(gravSign);
            p.y += p.yVel * tickScale * 0.9f;
            p.grounded = false;
        } else if (p.mode == 1 || p.mode == 7) { // Ship / Swing
            float const thrust = 0.958972f * 1.35f;
            float const target = holding ? -1.f : 1.f;
            p.yVel += target * thrust * static_cast<float>(gravSign) * tickScale;
            p.yVel *= std::pow(0.98f, tickScale);
            p.y += p.yVel * tickScale * 0.9f;
            p.grounded = false;
        } else if (p.mode == 3) { // UFO: hold = jump bursts while airborne ok once per press edge
            p.yVel -= p.gravity * p.gravityMod * static_cast<float>(gravSign) * tickScale;
            if (holding && acted && tick == options.delay) {
                p.yVel = boostImpulse(0) * static_cast<float>(-gravSign);
                jumped = true;
                out.jumpTick = tick;
            }
            p.y += p.yVel * tickScale * 0.9f;
            p.grounded = false;
        } else {
            // Cube / Ball / Robot / Spider — gravity + solid landing.
            if (!p.grounded)
                p.yVel -= p.gravity * p.gravityMod * static_cast<float>(gravSign) * tickScale;
            p.y += p.yVel * tickScale * 0.9f;
        }

        // Collide against frozen bodies.
        bool hitHazard = false;
        bool landed = false;
        for (auto const& body : world.bodies) {
            if (!aabbOverlap(p.x, p.y, p.w, p.h, body.x, body.y, body.w, body.h))
                continue;
            if (body.kind == 1) {
                hitHazard = true;
                break;
            }
            if (body.kind == 2) {
                // Pad: always fires on touch.
                p.yVel = boostImpulse(body.boost) * static_cast<float>(-gravSign);
                p.grounded = false;
                out.activatedBoost = true;
                jumped = true;
                if (body.boost == 3)
                    gravSign = -gravSign;
                continue;
            }
            if (body.kind == 3) {
                // Orb: only while Jump held (mirrors Shadow applyRingJump gate).
                if (!holding)
                    continue;
                p.yVel = boostImpulse(body.boost) * static_cast<float>(-gravSign);
                p.grounded = false;
                out.activatedBoost = true;
                jumped = true;
                if (body.boost == 3)
                    gravSign = -gravSign;
                continue;
            }
            // Solid: land from above / block from below relative to gravity.
            float const playerBottom = p.y;
            float const playerTop = p.y + p.h;
            float const solidTop = body.y + body.h;
            float const solidBottom = body.y;
            if (gravSign > 0) {
                // Normal gravity: land on top when falling.
                if (p.yVel <= 0.f && playerBottom <= solidTop && playerBottom >= solidTop - 12.f) {
                    p.y = solidTop;
                    p.yVel = 0.f;
                    p.grounded = true;
                    landed = true;
                } else if (p.yVel > 0.f && playerTop >= solidBottom &&
                           playerTop <= solidBottom + 12.f) {
                    p.y = solidBottom - p.h;
                    p.yVel = 0.f;
                }
            } else {
                if (p.yVel >= 0.f && playerTop >= solidBottom && playerTop <= solidBottom + 12.f) {
                    p.y = solidBottom - p.h;
                    p.yVel = 0.f;
                    p.grounded = true;
                    landed = true;
                } else if (p.yVel < 0.f && playerBottom <= solidTop &&
                           playerBottom >= solidTop - 12.f) {
                    p.y = solidTop;
                    p.yVel = 0.f;
                }
            }
        }
        if (!landed && (p.mode == 4 || p.mode == 1 || p.mode == 7))
            p.grounded = false;

        if (hitHazard || p.y < world.floorY - 80.f || p.y > world.ceilY + 80.f) {
            out.died = true;
            out.deathTick = tick;
            out.deathX = p.x + p.w * 0.5f;
            out.diedBeforeAction = !acted;
            return finish(out);
        }
    }

    return finish(out);
}

bool EbCpuPhysicsSim::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* label) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += ", ";
        report += label;
    };

    EbCpuPhysicsWorld world;
    world.valid = true;
    world.player.x = 100.f;
    world.player.y = 100.f;
    world.player.w = 30.f;
    world.player.h = 30.f;
    world.player.grounded = true;
    world.player.mode = 0;
    world.floorY = 0.f;
    world.ceilY = 400.f;

    // Open air — press at delay 0 should survive a short horizon.
    Options opt;
    opt.down = true;
    opt.delay = 0;
    opt.maxTicks = 16;
    auto alive = run(world, opt);
    expect(!alive.died, "open-air-alive");
    expect(alive.ticksRan == 16, "open-air-ticks");
    expect(alive.jumped, "open-air-jumped");

    // Hazard directly ahead — should die.
    EbCpuPhysicsBody hazard;
    hazard.x = 110.f;
    hazard.y = 100.f;
    hazard.w = 30.f;
    hazard.h = 30.f;
    hazard.kind = 1;
    world.bodies.push_back(hazard);
    auto dead = run(world, opt);
    expect(dead.died, "hazard-dies");
    expect(dead.deathTick >= 0, "hazard-death-tick");

    Options idle = opt;
    idle.down = false;
    idle.delay = 0;
    // Idle into hazard still dies.
    auto idleDead = run(world, idle);
    expect(idleDead.died, "idle-hazard-dies");

    if (fails == 0) {
        report = "CpuPhysicsSim selfCheck OK";
        return true;
    }
    report = fmt::format("CpuPhysicsSim selfCheck {} fails: {}", fails, report);
    return false;
}
