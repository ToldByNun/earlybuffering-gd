#include "shadow_world_dynamics.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../debug/lag_probe.hpp"
#include "../../../debug/perf_trace.hpp"
#include "../../assist/contracts/live_plan_contracts.hpp"
#include "../../trigger_world/trigger_world_store.hpp"
#include "../shadow_simulator.hpp"
#include "../sim_collision_policy.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

bool EbShadowWorldDynamics::isDynamicTriggerTarget(EbShadowSimulator const& simulator, int uniqueId) {
    return simulator.m_dynamicTargetIds.find(uniqueId) != simulator.m_dynamicTargetIds.end();
}

bool EbShadowWorldDynamics::isToggleTriggerTarget(EbShadowSimulator const& simulator, int uniqueId) {
    return simulator.m_toggleTargetIds.find(uniqueId) != simulator.m_toggleTargetIds.end();
}

bool EbShadowWorldDynamics::liveDynamicApplied(EbShadowSimulator const& simulator, int uniqueId) {
    return simulator.m_liveDynamicAppliedIds.find(uniqueId) !=
           simulator.m_liveDynamicAppliedIds.end();
}

void EbShadowWorldDynamics::noteUnknown(EbShadowSimulator& simulator, GameObject* object) {
    simulator.m_unknown = true;
    if (object && simulator.isToggleTriggerTarget(object->m_uniqueID)) {
        simulator.m_toggleUnknownHit = true;
    }
    if (!simulator.m_collectUnknownHist || !object)
        return;
    bool const hard = EbSimCollisionPolicy::unknownClass(object) == SimUnknownClass::Hard;
    int const oid = object->m_objectID;
    auto& bin = simulator.m_unknownHistMap[oid];
    if (bin.count == 0) {
        bin.objectId = oid;
        bin.objectType = static_cast<int>(object->m_objectType);
        bin.hard = hard;
    }
    // Prefer hard if any overlap of this objectId was hard.
    if (hard)
        bin.hard = true;
    bin.uniqueId = object->m_uniqueID;
    ++bin.count;
}

void EbShadowWorldDynamics::applyLiveDynamicNear(EbShadowSimulator& simulator, PlayLayer* layer,
                                               PlayerObject* ghost) {
    simulator.m_liveDynamicAppliedIds.clear();
    if (!layer || !ghost)
        return;
    if (!EbStageConfig::triggerBake())
        return;
    if (EbStageConfig::triggerBakeSkipDynamic())
        return;
    // Dual follow is ambiguous (P1 vs P2) — leave Unknown.
    if (layer->m_gameState.m_isDualMode && layer->m_player2)
        return;
    if (simulator.m_dynamicFollowJobs.empty())
        return;

    float const gx = ghost->getPositionX();
    float const gy = ghost->getPositionY();
    auto const wall0 = std::chrono::steady_clock::now();
    size_t applied = 0;

    for (auto& job : simulator.m_dynamicFollowJobs) {
        if (applied >= EbShadowSimulator::kMaxApplyPerTick)
            break;
        auto* effect = job.effect;
        if (!effect || effect->m_uniqueID != job.effectUniqueId)
            continue;

        for (auto const& t : job.targets) {
            if (applied >= EbShadowSimulator::kMaxApplyPerTick)
                break;
            auto const elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                                 std::chrono::steady_clock::now() - wall0)
                                                 .count();
            if (elapsedMicroseconds >= EbShadowSimulator::kApplyBudgetUs)
                break;

            auto* object = t.obj;
            if (!object || object->m_uniqueID != t.uniqueId)
                continue;
            // Prefer targets near the ghost; far follow targets stay Unknown.
            if (std::fabs(object->getPositionX() - gx) > EbShadowSimulator::kApplyMarginX)
                continue;

            float x = t.x;
            float y = t.y;
            if (job.followPlayerY || job.lockY) {
                y = gy + job.offsetY;
            }
            if (job.lockX) {
                x = gx + job.offsetX;
            }

            EbTriggerPose pose{};
            pose.tick = simulator.m_simAbsTick >= 0 ? simulator.m_simAbsTick : 0;
            pose.x = x;
            pose.y = y;
            pose.rotation = t.rotation;
            pose.scaleX = t.scaleX;
            pose.scaleY = t.scaleY;
            pose.active = t.nodeVisible;
            EbShadowSimulator::SavedTransform* saved = nullptr;
            for (auto& st : simulator.m_savedTransforms) {
                if (st.uniqueId == t.uniqueId) {
                    saved = &st;
                    break;
                }
            }
            bool const sameCached =
                saved && saved->hasAppliedPose && saved->appliedActive == pose.active &&
                saved->appliedX == pose.x && saved->appliedY == pose.y &&
                saved->appliedRotation == pose.rotation && saved->appliedScaleX == pose.scaleX &&
                saved->appliedScaleY == pose.scaleY;
            if (!sameCached) {
                (void)EbTriggerWorldStore::writePoseIfChanged(object, pose);
                if (saved) {
                    saved->dirty = true;
                    saved->hasAppliedPose = true;
                    saved->appliedActive = pose.active;
                    saved->appliedX = pose.x;
                    saved->appliedY = pose.y;
                    saved->appliedRotation = pose.rotation;
                    saved->appliedScaleX = pose.scaleX;
                    saved->appliedScaleY = pose.scaleY;
                }
            }
            simulator.m_liveDynamicAppliedIds.insert(t.uniqueId);
            ++applied;
        }
    }
}

void EbShadowWorldDynamics::applyTriggerWorldAtTick(EbShadowSimulator& simulator, PlayLayer* layer,
                                                  int absTick, PlayerObject* ghost) {
    simulator.m_triggerWorldAppliedThisTick = false;
    if (!layer || absTick < 0)
        return;
    if (!EbStageConfig::triggerBake())
        return;
    if (simulator.m_savedTransforms.empty())
        return;

    auto& store = EbTriggerWorldStore::get();
    if (!store.loaded() || !store.hasTracks())
        return;

    // Assist Pack AP-L3: plan soft wall already spent — skip cull/sort/apply for
    // this tick (Grief 942-track apply was still burning after wallExceeded).
    if (EbLivePlanContracts::liveTwApplySkippedForPlanWall(simulator.planWallExceeded()))
        return;

    EbLagProbe::Scope const lagScope("eb.trigger.apply");

    size_t const maxApply = EbLivePlanContracts::liveTwApplyMaxPerTick(
        store.trackCount(), simulator.m_bakeOffline, EbShadowSimulator::kMaxApplyPerTick);
    int const applyBudgetUs = EbLivePlanContracts::liveTwApplyBudgetUs(
        store.trackCount(), simulator.m_bakeOffline, EbShadowSimulator::kApplyBudgetUs);

    float const gx = ghost ? ghost->getPositionX() : 0.f;
    class EbTriggerApplyCandidate {
    public:
        EbShadowSimulator::SavedTransform const* st = nullptr;
        float dist = 0.f;
        // Named isNear — Windows headers define `near` as a macro.
        bool isNear = false;

    private:
    };
    std::vector<EbTriggerApplyCandidate> cands;
    cands.reserve(128);
    constexpr float kCullMarginX = EbShadowSimulator::kApplyMarginX * 2.5f;
    for (auto const& st : simulator.m_savedTransforms) {
        auto* object = st.obj;
        if (!object || object->m_uniqueID != st.uniqueId)
            continue;
        float const distNow = std::fabs(object->getPositionX() - gx);
        float const distBase = std::fabs(st.x - gx);
        float const dist = std::min(distNow, distBase);
        if (dist > kCullMarginX)
            continue;
        EbTriggerApplyCandidate c;
        c.st = &st;
        c.dist = dist;
        c.isNear = dist <= EbShadowSimulator::kApplyMarginX;
        cands.push_back(c);
    }
    // Near objects first, then closer far objects — skip remainder if budget/cap.
    std::sort(cands.begin(), cands.end(),
              [](EbTriggerApplyCandidate const& a, EbTriggerApplyCandidate const& b) {
                  if (a.isNear != b.isNear)
                      return a.isNear && !b.isNear;
                  return a.dist < b.dist;
              });

    auto const wall0 = std::chrono::steady_clock::now();
    size_t applied = 0;
    for (auto const& c : cands) {
        if (applied >= maxApply)
            break;
        // Soft wall can trip mid-sort apply; stop further TW writes this tick.
        if (EbLivePlanContracts::liveTwApplySkippedForPlanWall(simulator.planWallExceeded()))
            break;
        auto const elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                             std::chrono::steady_clock::now() - wall0)
                                             .count();
        if (elapsedMicroseconds >= applyBudgetUs)
            break;

        auto* object = c.st->obj;
        EbTriggerPose pose{};
        bool ok = false;
        if (simulator.m_poseRemapDt > 0.f) {
            ok = store.samplePose(c.st->uniqueId, absTick, simulator.m_poseRemapDt, pose);
        } else {
            ok = store.samplePose(c.st->uniqueId, absTick, pose);
        }
        if (!ok)
            continue;
        bool const sameCached =
            c.st->hasAppliedPose && c.st->appliedActive == pose.active &&
            c.st->appliedX == pose.x && c.st->appliedY == pose.y &&
            c.st->appliedRotation == pose.rotation && c.st->appliedScaleX == pose.scaleX &&
            c.st->appliedScaleY == pose.scaleY;
        if (!sameCached) {
            (void)EbTriggerWorldStore::writePoseIfChanged(object, pose);
            c.st->dirty = true;
            c.st->hasAppliedPose = true;
            c.st->appliedActive = pose.active;
            c.st->appliedX = pose.x;
            c.st->appliedY = pose.y;
            c.st->appliedRotation = pose.rotation;
            c.st->appliedScaleX = pose.scaleX;
            c.st->appliedScaleY = pose.scaleY;
        }
        ++applied;
    }
    // Far / over-budget tracks hold last written pose (or capture baseline).
    simulator.m_triggerWorldAppliedThisTick = true;
}
