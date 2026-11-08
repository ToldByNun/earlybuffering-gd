#include "shadow_plan_policy.hpp"
#include "../../../config/stage_config.hpp"
#include "../../assist/planning/assist_horizon.hpp"
#include "../foresight/travel_foresight.hpp"
#include "../shadow_simulator.hpp"
#include "../sim_trust.hpp"
#include "simulation_plan.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

ShadowSimulator::Model ShadowPlanPolicy::modelFor(PlayerObject* player, int limit, float capMul) {
    ShadowSimulator::Model m;
    if (!player)
        return m;
    auto const params = StageConfig::paramsFor(player);
    m.slack = std::max(0, params.slack);

    // Commit 15: planCap from AssistHorizon — long only when SimTrust OK.
    bool const trustOk = SimTrust::get().okAt(player->getPositionX());
    auto const kind = StageConfig::kindOf(player);
    auto const caps = AssistHorizon::resolve(kind, trustOk, limit, capMul, params.cap,
                                             StageConfig::lookAheadCap());
    m.cap = caps.planCap;

    if (player->m_isDart) {
        m.continuous = true;
    } else if (player->m_isShip || player->m_isSwing) {
        m.continuous = true;
    } else if (player->m_isBird) {
        m.continuous = true;
        m.tapRelease = true;
    } else if (player->m_isSpider) {
        m.continuous = true;
        m.tapRelease = true;
    } else if (player->m_isRobot) {
        m.tapRelease = false;
    } else {
        m.tapRelease = true;
    }
    return m;
}

bool ShadowPlanPolicy::isHazardLike(GameObject* object) {
    if (!object)
        return false;
    switch (object->m_objectType) {
    case GameObjectType::Hazard:
    case GameObjectType::AnimatedHazard:
        return true;
    default:
        return false;
    }
}

ThreatZone ShadowPlanPolicy::buildThreatZone(PlayLayer* layer, float deathX, GameObject* deathObj) {
    ThreatZone z;
    z.valid = true;
    z.startX = deathX - 20.f;
    z.endX = deathX + 20.f;
    if (deathObj) {
        float const x = deathObj->getPositionX();
        float half = deathObj->getContentSize().width * std::fabs(deathObj->getScaleX()) * 0.5f;
        if (half < 12.f)
            half = 12.f;
        z.startX = std::min(z.startX, x - half);
        z.endX = std::max(z.endX, x + half);
    }
    if (!layer || !layer->m_objectLayer)
        return z;

    auto* children = layer->m_objectLayer->getChildren();
    if (!children)
        return z;

    class HazardSpan {
    public:
        float lo = 0.f;
        float hi = 0.f;

    private:
    };
    std::vector<HazardSpan> localHazards;
    localHazards.reserve(128);
    constexpr float kMaxClusterRadiusX = 800.f;
    float const winLo = z.startX - kMaxClusterRadiusX;
    float const winHi = z.endX + kMaxClusterRadiusX;
    for (unsigned index = 0; index < children->count(); ++index) {
        auto* object = geode::prelude::typeinfo_cast<GameObject*>(children->objectAtIndex(index));
        if (!ShadowSimulator::isHazardLike(object))
            continue;
        float const x = object->getPositionX();
        if (x < winLo || x > winHi)
            continue;
        float half = object->getContentSize().width * std::fabs(object->getScaleX()) * 0.5f;
        if (half < 10.f)
            half = 10.f;
        localHazards.push_back({x - half, x + half});
    }

    bool grew = true;
    int guard = 0;
    while (grew && guard++ < 32) {
        grew = false;
        for (auto const& h : localHazards) {
            if (h.hi + ShadowSimulator::kClusterGapX < z.startX ||
                h.lo - ShadowSimulator::kClusterGapX > z.endX)
                continue;
            if (h.lo < z.startX) {
                z.startX = h.lo;
                grew = true;
            }
            if (h.hi > z.endX) {
                z.endX = h.hi;
                grew = true;
            }
        }
    }
    return z;
}

bool ShadowPlanPolicy::spatialOrbHint(PlayLayer* layer, PlayerObject* real, int limit,
                                      float deltaTime) {
    if (!layer || !real || deltaTime <= 0.f)
        return false;
    int slack = 0;
    if (real) {
        slack = StageConfig::paramsFor(real).slack;
    }
    return TravelForesight::hasOrbOrPadAhead(layer, real, limit, deltaTime, slack);
}

bool ShadowPlanPolicy::spatialHazardProximity(PlayLayer* layer, PlayerObject* real,
                                              float proximityX) {
    if (!layer || !real || !layer->m_objectLayer || proximityX <= 0.f)
        return false;

    float const playerX = real->getPositionX();
    float const playerY = real->getPositionY();
    float const yReach = real->m_isShip || real->m_isDart || real->m_isSwing ? 100.f : 70.f;

    auto* children = layer->m_objectLayer->getChildren();
    if (!children)
        return false;

    for (unsigned index = 0; index < children->count(); ++index) {
        auto* object = geode::prelude::typeinfo_cast<GameObject*>(children->objectAtIndex(index));
        if (!ShadowSimulator::isHazardLike(object))
            continue;

        float const objectX = object->getPositionX();
        float const objectY = object->getPositionY();
        float half = object->getContentSize().width * std::fabs(object->getScaleX()) * 0.5f;
        if (half < 8.f)
            half = 8.f;
        float const oLo = objectX - half;
        float const oHi = objectX + half;
        // Ahead of player, overlapping [px, px+proximityX].
        if (oHi < playerX - 4.f)
            continue;
        if (oLo > playerX + proximityX)
            continue;
        if (std::fabs(objectY - playerY) > yReach)
            continue;
        return true;
    }
    return false;
}

bool ShadowPlanPolicy::shouldWaveHazardProximityPass(bool isWave, bool nearHazard, bool noOrbTarget,
                                                     bool foresightOrbPad, bool pressDown) {
    // Release assist = delay the UP (keep holding). Never early-Pass that near hazards.
    if (!pressDown)
        return false;
    if (!isWave)
        return false;
    if (!nearHazard)
        return false;
    if (!noOrbTarget)
        return false;
    // Miss-assist: orb/pad in travel cone → keep searching, do not Pass.
    if (!(foresightOrbPad)) {
        return true;
    }
    return false;
}

bool ShadowPlanPolicy::shouldWaveFutileDelayPass(bool isWave, bool orbIntent, int bestSurviveBin,
                                                 float bestEndXGain, bool foresightOrbPad,
                                                 bool pressDown) {
    if (!pressDown)
        return false;
    if (!isWave)
        return false;
    if (orbIntent)
        return false;
    if (foresightOrbPad)
        return false;
    if (bestSurviveBin > 0)
        return false;
    if (!(bestEndXGain >= ShadowSimulator::kFutileEndXGain)) {
        return true;
    }
    return false;
}

bool ShadowPlanPolicy::deathLooksLikeAscent(float playerY, float deathY, bool upsideDown,
                                            float marginY) {
    if (marginY < 0.f)
        marginY = 0.f;
    if (upsideDown) {
        // Flipped: "ceiling" is below in world Y.
        return deathY < playerY - marginY;
    }
    return deathY > playerY + marginY;
}

bool ShadowPlanPolicy::classifyIdleSafer(bool continuous, bool pressDown, bool baselineDied,
                                         int baselineDeathTick, float baselineDeathX, bool d0Died,
                                         int d0DeathTick, float d0DeathX) {
    if (!continuous || !pressDown)
        return false;
    // Idle must look better than pressing now.
    if (!d0Died)
        return false;
    if (!baselineDied)
        return true;
    if (baselineDeathTick < 0 || d0DeathTick < 0)
        return !baselineDied;
    if (baselineDeathTick > d0DeathTick)
        return true;
    if (baselineDeathTick == d0DeathTick && baselineDeathX > d0DeathX + 8.f) {
        return true;
    }
    return false;
}

bool ShadowPlanPolicy::classifyAscentUseful(bool continuous, bool pressDown, bool d0Viable,
                                            int d0SurviveBin, float d0EndX, bool laterViable,
                                            int laterSurviveBin, float laterEndX, bool laterAlive) {
    if (!continuous || !pressDown)
        return false;
    // Dead / soft Landing-LateCrash ghosts must not invent ascentUseful.
    if (!laterViable || !laterAlive)
        return false;
    if (!d0Viable)
        return true;
    if (laterSurviveBin > d0SurviveBin)
        return true;
    if (!(laterEndX > d0EndX + ShadowSimulator::kFutileEndXGain)) {
        return false;
    }
    return true;
}

int ShadowPlanPolicy::pickPreferredRelease(int earliest, int latest, bool continuous) {
    if (earliest < 0)
        return -1;
    if (latest < earliest)
        latest = earliest;
    if (!(continuous)) {
        return earliest + (latest - earliest) / 2;
    }
    return earliest;
}

int ShadowPlanPolicy::autoRobustKHints(bool waveLike, bool isWave, float playerSpeed,
                                       float dtVariance, bool spamDense) {
    int offset = ShadowSimulator::kWindowMarginK; // 1
    if (waveLike)
        offset = 2;
    // Wave spam corridors: keep at least 2; at speed bump to 3 so ±1 neighbors
    // of the pick stay clear (Tight ticks-wave=2 may still fall back — see docs).
    if (isWave && spamDense) {
        offset = std::max(offset, 2);
        if (playerSpeed > 1.05f)
            offset = std::max(offset, 3);
    }
    if (playerSpeed > 1.1f)
        offset = std::max(offset, 2);
    if (playerSpeed > 1.5f)
        offset = 3;
    if (dtVariance > 0.08f)
        offset = std::min(3, offset + 1);
    return std::clamp(offset, 1, 3);
}

int ShadowPlanPolicy::autoRobustK(PlayerObject* real, float dtVariance, bool spamDense) {
    bool waveLike = false;
    bool isWave = false;
    float speed = 1.f;
    if (real) {
        isWave = real->m_isDart;
        waveLike = real->m_isDart || real->m_isShip || real->m_isSwing;
        speed = real->m_playerSpeed;
    }
    return ShadowSimulator::autoRobustKHints(waveLike, isWave, speed, dtVariance, spamDense);
}
