#include "adaptive_resume_runner.hpp"
#include <algorithm>
#include <fmt/format.h>

EbShadowSimulator::RunResult EbAdaptiveResumeRunner::runQuantum(
    EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real, bool down, int limit,
    float deltaTime, float firstDt, EbShadowSimulator::Model const& model, int releaseAfter,
    EbThreatZone const* zone, int* tickBudgetLeft, EbAdaptiveCandidateSlot& slot, int quantumTicks) {
    EbShadowSimulator::RunResult r;
    if (!layer || !real || !simulator.m_shadow || quantumTicks <= 0)
        return r;

    int const delay = slot.delay;
    int const absoluteCap = std::max(std::max(0, limit), delay) + std::max(1, model.cap);
    bool const resuming = slot.hasCheckpoint();
    int startTick = 0;
    bool acted = false;
    bool tapReleased = false;

    if (resuming) {
        auto* snap = simulator.snapshotSlot(slot.snapshotSlot);
        if (!snap) {
            slot.invalidateCheckpoint();
            return r;
        }
        simulator.copyPlayerState(simulator.m_shadow, snap);
        startTick = slot.ticksSimulated;
        acted = slot.acted;
        tapReleased = slot.tapReleased;
        r = slot.lastRaw;
        // Extension segment counters start at 0; merge into slot at end.
        r.ticksRan = 0;
        r.died = false;
        r.unknown = false;
        r.unknownTrunc = false;
        r.deathTick = -1;
        r.unknownTick = -1;
        r.jumped = false;
        r.jumpTick = -1;
        r.landedAfterAction = false;
        r.zoneCleared = false;
        r.ticksBeforeNeutral = 0;
    } else {
        simulator.copyPlayerState(simulator.m_shadow, real);
        acted = false;
        tapReleased = false;
    }

    simulator.m_dead = false;
    simulator.m_jumped = resuming ? slot.lastRaw.jumped : false;
    simulator.m_unknown = false;
    simulator.m_toggleUnknownHit = false;
    if (!resuming) {
        simulator.m_activatedRing = false;
        simulator.m_ringCount = 0;
    } else {
        simulator.m_activatedRing = slot.activatedRing;
        simulator.m_ringCount = slot.lastRaw.ringCount;
    }
    simulator.m_deathObject = nullptr;

    auto* shadowPlayer = simulator.m_shadow;
    int const jumpIdx = static_cast<int>(PlayerButton::Jump);
    if (resuming) {
        shadowPlayer->m_holdingButtons[jumpIdx] = slot.holdingJump;
    } else {
        shadowPlayer->m_holdingButtons[jumpIdx] = !down;
    }

    int groundedStreak = 0;
    int settledAfterClear = 0;
    bool pastZone = resuming && slot.zoneCleared;
    if (pastZone)
        r.zoneCleared = true;
    bool const initialGravity = shadowPlayer->m_isUpsideDown;
    bool ballFlipped = false;
    bool ballAirborne = false;
    int ballLandingTicks = 0;

    int const endTick = std::min(absoluteCap, startTick + quantumTicks);

    auto finishSegment = [&](EbShadowSimulator::RunResult output) {
        output.activatedRing = simulator.m_activatedRing || output.activatedRing;
        output.ringCount = std::max(output.ringCount, simulator.m_ringCount);
        output.endX = shadowPlayer->getPositionX();
        output.wasDashing = shadowPlayer->m_isDashing || output.wasDashing;
        output.toggleUnknown = simulator.m_toggleUnknownHit || output.toggleUnknown;
        if (output.toggleUnknown)
            output.unknownTrunc = true;
        output.ticksRan =
            std::max(output.ticksRan, output.deathTick >= 0 ? output.deathTick + 1 : 0);
        if (output.ticksBeforeNeutral <= 0)
            output.ticksBeforeNeutral = output.ticksRan;
        return output;
    };

    auto spend = [&]() -> bool {
        if (!tickBudgetLeft)
            return false;
        if (*tickBudgetLeft <= 0)
            return true;
        --(*tickBudgetLeft);
        return false;
    };

    // Fresh delay>0 pre-collide only when starting from tick 0 (mirrors runOne).
    if (!resuming && delay > 0 && startTick == 0) {
        simulator.m_orbOverlapTick = false;
        simulator.m_simAbsTick = simulator.m_simTickBase;
        simulator.stepPreCollide(layer, shadowPlayer, deltaTime);
        if (simulator.m_dead) {
            r.ticksRan = 1;
            r.died = true;
            r.deathTick = 0;
            r.deathX = shadowPlayer->getPositionX();
            r.deathObject = simulator.m_deathObject;
            r.diedBeforeAction = true;
            r = finishSegment(r);
            slot.applyQuantumResult(r, -1, 1, simulator.m_simAbsTick, false, false, false, false);
            return r;
        }
        if (simulator.m_unknown) {
            r.ticksRan = 1;
            r.unknown = true;
            r.unknownTrunc = true;
            r.unknownTick = 0;
            r = finishSegment(r);
            slot.applyQuantumResult(r, -1, 1, simulator.m_simAbsTick, false, false, false, false);
            return r;
        }
    }

    EbShadowSimulator::RunResult segment{};
    for (int tick = startTick; tick < endTick; ++tick) {
        if (spend())
            break;
        float const stepDt = (tick == 0) ? firstDt : deltaTime;
        int const segmentTick = tick - startTick;
        segment.ticksRan = segmentTick + 1;
        simulator.m_orbOverlapTick = false;
        simulator.m_simAbsTick = simulator.m_simTickBase + tick;

        if (!acted && tick == delay) {
            acted = true;
            if (down) {
                shadowPlayer->pushButton(PlayerButton::Jump);
                shadowPlayer->m_holdingButtons[jumpIdx] = true;
            } else {
                shadowPlayer->releaseButton(PlayerButton::Jump);
                shadowPlayer->m_holdingButtons[jumpIdx] = false;
            }
        } else if (acted && down) {
            if (releaseAfter >= 0 && !tapReleased && tick == delay + releaseAfter) {
                tapReleased = true;
                shadowPlayer->releaseButton(PlayerButton::Jump);
            } else if (releaseAfter < 0 && model.tapRelease && !tapReleased && simulator.m_jumped) {
                bool const touchingOrb =
                    simulator.m_orbOverlapTick ||
                    (shadowPlayer->m_touchingRings && shadowPlayer->m_touchingRings->count() > 0);
                bool const dashing = shadowPlayer->m_isDashing;
                bool const justHitOrb = simulator.m_activatedRing && tick <= delay + 4;
                if (!touchingOrb && !dashing && !justHitOrb) {
                    tapReleased = true;
                    shadowPlayer->releaseButton(PlayerButton::Jump);
                }
            }
        }

        simulator.stepPhysics(layer, shadowPlayer, stepDt, deltaTime);
        if (simulator.m_dead) {
            segment.died = true;
            segment.deathTick = segmentTick;
            segment.deathX = shadowPlayer->getPositionX();
            segment.deathObject = simulator.m_deathObject;
            segment.diedBeforeAction = !acted;
            segment = finishSegment(segment);
            auto const absolute =
                resuming ? EbAdaptiveCandidateSlot::mergeExtendResults(slot.lastRaw, segment)
                         : segment;
            slot.applyQuantumResult(absolute, -1, startTick + segment.ticksRan,
                                    simulator.m_simAbsTick, acted, tapReleased,
                                    shadowPlayer->m_holdingButtons[jumpIdx], false);
            return absolute;
        }
        if (simulator.m_unknown) {
            segment.unknown = true;
            segment.unknownTrunc = true;
            segment.unknownTick = segmentTick;
            segment = finishSegment(segment);
            auto const absolute =
                resuming ? EbAdaptiveCandidateSlot::mergeExtendResults(slot.lastRaw, segment)
                         : segment;
            slot.applyQuantumResult(absolute, -1, startTick + segment.ticksRan,
                                    simulator.m_simAbsTick, acted, tapReleased,
                                    shadowPlayer->m_holdingButtons[jumpIdx], false);
            return absolute;
        }

        if (model.stopAtBallLanding && acted && shadowPlayer->m_isBall) {
            ballFlipped = ballFlipped || shadowPlayer->m_isUpsideDown != initialGravity;
            bool const grounded = shadowPlayer->m_isOnGround || shadowPlayer->m_isOnGround2;
            if (ballFlipped && !grounded)
                ballAirborne = true;
            ballLandingTicks = ballAirborne && grounded ? ballLandingTicks + 1 : 0;
            if (ballLandingTicks >= 2) {
                segment.landedAfterAction = true;
                segment = finishSegment(segment);
                auto const absolute =
                    resuming ? EbAdaptiveCandidateSlot::mergeExtendResults(slot.lastRaw, segment)
                             : segment;
                // Landing stop is terminal for this quantum — keep checkpoint if still alive.
                int const snap = simulator.captureSnapshot(shadowPlayer);
                slot.applyQuantumResult(absolute, snap, startTick + segment.ticksRan,
                                        simulator.m_simAbsTick, acted, tapReleased,
                                        shadowPlayer->m_holdingButtons[jumpIdx], snap >= 0);
                return absolute;
            }
        }

        if (simulator.m_jumped && !segment.jumped) {
            segment.jumped = true;
            segment.jumpTick = segmentTick;
        }
        if (shadowPlayer->m_isDashing)
            segment.wasDashing = true;

        if (zone && zone->valid && !pastZone) {
            if (shadowPlayer->getPositionX() > zone->endX) {
                bool settled = false;
                if (model.continuous) {
                    settledAfterClear++;
                    settled = settledAfterClear >= 8;
                } else {
                    bool const grounded = shadowPlayer->m_isOnGround || shadowPlayer->m_isOnGround2;
                    if (grounded)
                        ++groundedStreak;
                    else
                        groundedStreak = 0;
                    settled = groundedStreak >= 2;
                }
                if (settled) {
                    pastZone = true;
                    segment.zoneCleared = true;
                    segment.ticksBeforeNeutral = segmentTick + 1;
                }
            } else {
                settledAfterClear = 0;
            }
        }

        if (pastZone && !model.continuePastClear &&
            segmentTick + 1 - segment.ticksBeforeNeutral > 24) {
            segment = finishSegment(segment);
            auto const absolute =
                resuming ? EbAdaptiveCandidateSlot::mergeExtendResults(slot.lastRaw, segment)
                         : segment;
            int const snap = simulator.captureSnapshot(shadowPlayer);
            slot.applyQuantumResult(absolute, snap, startTick + segment.ticksRan,
                                    simulator.m_simAbsTick, acted, tapReleased,
                                    shadowPlayer->m_holdingButtons[jumpIdx], snap >= 0);
            return absolute;
        }
    }

    segment = finishSegment(segment);
    auto const absolute =
        resuming ? EbAdaptiveCandidateSlot::mergeExtendResults(slot.lastRaw, segment) : segment;
    bool const hitCap = startTick + segment.ticksRan >= absoluteCap;
    int snap = -1;
    bool keep = false;
    if (!absolute.died && !absolute.unknown && !absolute.unknownTrunc && !hitCap) {
        snap = simulator.captureSnapshot(shadowPlayer);
        keep = snap >= 0;
    }
    slot.applyQuantumResult(absolute, snap, startTick + std::max(0, segment.ticksRan),
                            simulator.m_simAbsTick, acted, tapReleased,
                            shadowPlayer->m_holdingButtons[jumpIdx], keep);
    slot.terminalAliveCap = hitCap && slot.autonomousAlive;
    slot.screened = true;
    return absolute;
}

bool EbAdaptiveResumeRunner::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += "; ";
        report += name;
    };

    // Null / empty quantum must no-op without crashing (no PlayLayer in PlanGold).
    EbShadowSimulator::Model model{};
    model.cap = 32;
    EbAdaptiveCandidateSlot slot;
    slot.bindDelay(2);
    auto const empty = runQuantum(EbShadowSimulator::get(), nullptr, nullptr, true, 4, 1.f / 240.f,
                                  1.f / 240.f, model, -1, nullptr, nullptr, slot, 16);
    expect(empty.ticksRan == 0, "null-noop");
    expect(!slot.hasCheckpoint(), "null-no-cp");

    // Contiguous 16+16 merge identity (live physics parity lands with Adaptive search).
    std::string slotReport;
    expect(EbAdaptiveCandidateSlot::selfCheck(slotReport), "slot-16-plus-16");

    if (fails == 0)
        report = "AdaptiveResumeRunner OK";
    else
        report = fmt::format("AdaptiveResumeRunner {} fails: {}", fails, report);
    return fails == 0;
}
