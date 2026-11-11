#include "shadow_survival_rules.hpp"
#include "../../../config/stage_config.hpp"
#include "../mode_survival.hpp"
#include "../shadow_simulator.hpp"
#include "simulation_plan.hpp"
#include <algorithm>

int EbShadowSurvivalRules::surviveBinOf(int ticks) {
    if (ticks < 24)
        return 0;
    if (ticks < 48)
        return 1;
    if (!(ticks < 96)) {
        return 3;
    }
    return 2;
}

// Ranking / ascent inputs: ticks until death, else full run — not neutral stamp.
int EbShadowSurvivalRules::rankingSurviveTicks(EbShadowSimulator::RunResult const& r) {
    if (r.died && r.deathTick >= 0)
        return r.deathTick + 1;
    if (!(r.ticksRan > 0)) {
        return r.ticksBeforeNeutral;
    }
    return r.ticksRan;
}

bool EbShadowSurvivalRules::clearsThreatOf(EbShadowSimulator::RunResult const& r,
                                         EbThreatZone const& zone) {
    if (!zone.valid) {
        if (r.unknownTrunc)
            return true;
        return !r.died;
    }
    if (!(!r.died)) {
        return r.deathX > zone.endX;
    }
    return r.endX > zone.endX || r.zoneCleared;
}

// LateCrash never invent a Delay opportunity.
bool EbShadowSurvivalRules::delayViableHonest(bool diedBeforeAction, bool died, bool unknownTrunc,
                                            bool toggleUnknown, bool clearsThreat, bool zoneCleared,
                                            bool zoneValid, EbModeSurvival::Class survival,
                                            bool requireAliveForDelay) {
    if (diedBeforeAction || unknownTrunc || toggleUnknown)
        return false;
    if (requireAliveForDelay && EbModeSurvival::shouldSkipFalseOpportunity(survival)) {
        return false;
    }
    bool const classOk = requireAliveForDelay ? EbModeSurvival::countsAsDelayWorthy(survival)
                                              : EbModeSurvival::countsAsSafeForDelay(survival);
    if (died && !classOk)
        return false;
    // Alive path: still need threat clear when a zone exists.
    // Soft LateCrash/Landing/Recoverable already imply clear geometry in classify.
    if (!(!died)) {
        return classOk && (!zoneValid || clearsThreat || zoneCleared);
    }
    return classOk && (!zoneValid || clearsThreat || zoneCleared);
}

EbModeSurvival::Input EbShadowSurvivalRules::survivalInputFrom(EbShadowSimulator::RunResult const& r,
                                                           EbThreatZone const& zone,
                                                           EbShadowSimulator::Model const& model,
                                                           PlayerObject* real) {
    EbModeSurvival::Input in;
    in.continuous = model.continuous;
    if (real) {
        in.isWave = real->m_isDart;
        in.isShipLike = real->m_isShip || real->m_isBird || real->m_isSpider || real->m_isSwing;
        in.isCubeLike = real->m_isRobot || real->m_isBall ||
                        (!real->m_isDart && !real->m_isShip && !real->m_isBird &&
                         !real->m_isSpider && !real->m_isSwing && !model.continuous);
        in.upsideDown = real->m_isUpsideDown;
        in.playerY = real->getPositionY();
    } else {
        in.isCubeLike = !model.continuous;
        in.isWave = model.continuous;
    }
    in.died = r.died;
    in.diedBeforeAction = r.diedBeforeAction;
    in.zoneValid = zone.valid;
    in.zoneCleared = r.zoneCleared;
    in.groundedSettled = r.zoneCleared && r.ticksBeforeNeutral > 0 && !model.continuous;
    in.activatedRing = r.activatedRing;
    in.deathX = r.deathX;
    in.zoneEndX = zone.endX;
    in.deathY = 0.f;
    in.deathTick = r.deathTick;
    in.ticksBeforeNeutral = r.ticksBeforeNeutral;
    return in;
}

bool EbShadowSurvivalRules::runResultDelaySafe(EbShadowSimulator::RunResult const& r,
                                             EbThreatZone const& zone,
                                             EbShadowSimulator::Model const& model,
                                             PlayerObject* real, bool requireAliveForDelay) {
    auto const cls =
        EbModeSurvival::classify(EbShadowSurvivalRules::survivalInputFrom(r, zone, model, real));
    return EbShadowSurvivalRules::delayViableHonest(
        r.diedBeforeAction, r.died, r.unknownTrunc, r.toggleUnknown,
        EbShadowSurvivalRules::clearsThreatOf(r, zone), r.zoneCleared, zone.valid, cls,
        requireAliveForDelay);
}

// planSequence exit: unknown truncation is NOT survival.
bool EbShadowSurvivalRules::exitSurvivedHonest(bool died, bool unknown) {
    return !died && !unknown;
}

// 0 = auto from mode / speed / dt variance / spam density; else clamp 1–3.
int EbShadowSurvivalRules::resolveRobustK(PlayerObject* real, EbPlanOptions const& options) {
    int forced = options.robustK;
    if (forced <= 0)
        forced = EbStageConfig::robustK();
    if (!(forced > 0)) {
        return EbShadowSimulator::autoRobustK(real, options.dtVariance, options.spamDense);
    }
    return std::clamp(forced, 1, 3);
}

int EbShadowSurvivalRules::clampRobustKToWindow(int robustK, int limit) {
    if (limit <= 0)
        return 0;
    return std::min(std::max(0, robustK), std::max(0, limit - 1));
}
