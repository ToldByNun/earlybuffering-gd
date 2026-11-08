#include "shadow_world_dynamics.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../debug/lag_probe.hpp"
#include "../../../debug/perf_trace.hpp"
#include "../../trigger_world/trigger_world_store.hpp"
#include "../shadow_simulator.hpp"
#include "../sim_collision_policy.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

bool ShadowWorldDynamics::isDynamicTriggerTarget(ShadowSimulator const& simulator, int uniqueId) {
    return simulator.m_dynamicTargetIds.find(uniqueId) != simulator.m_dynamicTargetIds.end();
}

bool ShadowWorldDynamics::isToggleTriggerTarget(ShadowSimulator const& simulator, int uniqueId) {
    return simulator.m_toggleTargetIds.find(uniqueId) != simulator.m_toggleTargetIds.end();
}

bool ShadowWorldDynamics::liveDynamicApplied(ShadowSimulator const& simulator, int uniqueId) {
    return simulator.m_liveDynamicAppliedIds.find(uniqueId) !=
           simulator.m_liveDynamicAppliedIds.end();
}

void ShadowWorldDynamics::noteUnknown(ShadowSimulator& simulator, GameObject* object) {
    simulator.m_unknown = true;
    if (object && simulator.isToggleTriggerTarget(object->m_uniqueID)) {
        simulator.m_toggleUnknownHit = true;
    }
    if (!simulator.m_collectUnknownHist || !object)
        return;
    bool const hard = SimCollisionPolicy::unknownClass(object) == SimUnknownClass::Hard;
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

void ShadowWorldDynamics::applyLiveDynamicNear(ShadowSimulator& simulator, PlayLayer* layer,
                                               PlayerObject* ghost) {
    simulator.m_liveDynamicAppliedIds.clear();
    if (!layer || !ghost)
        return;
    if (!StageConfig::triggerBake())
        return;
    if (StageConfig::triggerBakeSkipDynamic())
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
        if (applied >= ShadowSimulator::kMaxApplyPerTick)
            break;
        auto* effect = job.effect;
        if (!effect || effect->m_uniqueID != job.effectUniqueId)
            continue;

        for (auto const& t : job.targets) {
            if (applied >= ShadowSimulator::kMaxApplyPerTick)
                break;
            auto const elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                                 std::chrono::steady_clock::now() - wall0)
                                                 .count();
            if (elapsedMicroseconds >= ShadowSimulator::kApplyBudgetUs)
                break;

            auto* object = t.obj;
            if (!object || object->m_uniqueID != t.uniqueId)
                continue;
            // Prefer targets near the ghost; far follow targets stay Unknown.
            if (std::fabs(object->getPositionX() - gx) > ShadowSimulator::kApplyMarginX)
                continue;

            float x = t.x;
            float y = t.y;
            if (job.followPlayerY || job.lockY) {
                y = gy + job.offsetY;
            }
            if (job.lockX) {
                x = gx + job.offsetX;
            }

            TriggerPose pose{};
            pose.tick = simulator.m_simAbsTick >= 0 ? simulator.m_simAbsTick : 0;
            pose.x = x;
            pose.y = y;
            pose.rotation = t.rotation;
            pose.scaleX = t.scaleX;
            pose.scaleY = t.scaleY;
            pose.active = t.nodeVisible;
            TriggerWorldStore::writePose(object, pose);
            for (auto const& st : simulator.m_savedTransforms) {
                if (st.uniqueId == t.uniqueId) {
                    st.dirty = true;
                    break;
                }
            }
            simulator.m_liveDynamicAppliedIds.insert(t.uniqueId);
            ++applied;
        }
    }
}

void ShadowWorldDynamics::applyTriggerWorldAtTick(ShadowSimulator& simulator, PlayLayer* layer,
                                                  int absTick, PlayerObject* ghost) {
    simulator.m_triggerWorldAppliedThisTick = false;
    if (!layer || absTick < 0)
        return;
    if (!StageConfig::triggerBake())
        return;
    if (simulator.m_savedTransforms.empty())
        return;

    auto& store = TriggerWorldStore::get();
    if (!store.loaded() || !store.hasTracks())
        return;

    LagProbe::Scope const lagScope("eb.trigger.apply");

    float const gx = ghost ? ghost->getPositionX() : 0.f;
    class SimulationCandidate {
    public:
        ShadowSimulator::SavedTransform const* st = nullptr;
        float dist = 0.f;
        // Named isNear — Windows headers define `near` as a macro.
        bool isNear = false;

    private:
    };
    std::vector<SimulationCandidate> cands;
    cands.reserve(128);
    constexpr float kCullMarginX = ShadowSimulator::kApplyMarginX * 2.5f;
    for (auto const& st : simulator.m_savedTransforms) {
        auto* object = st.obj;
        if (!object || object->m_uniqueID != st.uniqueId)
            continue;
        float const distNow = std::fabs(object->getPositionX() - gx);
        float const distBase = std::fabs(st.x - gx);
        float const dist = std::min(distNow, distBase);
        if (dist > kCullMarginX)
            continue;
        SimulationCandidate c;
        c.st = &st;
        c.dist = dist;
        c.isNear = dist <= ShadowSimulator::kApplyMarginX;
        cands.push_back(c);
    }
    // Near objects first, then closer far objects — skip remainder if budget/cap.
    std::sort(cands.begin(), cands.end(),
              [](SimulationCandidate const& a, SimulationCandidate const& b) {
                  if (a.isNear != b.isNear)
                      return a.isNear && !b.isNear;
                  return a.dist < b.dist;
              });

    auto const wall0 = std::chrono::steady_clock::now();
    size_t applied = 0;
    for (auto const& c : cands) {
        if (applied >= ShadowSimulator::kMaxApplyPerTick)
            break;
        auto const elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                             std::chrono::steady_clock::now() - wall0)
                                             .count();
        if (elapsedMicroseconds >= ShadowSimulator::kApplyBudgetUs)
            break;

        auto* object = c.st->obj;
        TriggerPose pose{};
        bool ok = false;
        if (simulator.m_poseRemapDt > 0.f) {
            ok = store.samplePose(c.st->uniqueId, absTick, simulator.m_poseRemapDt, pose);
        } else {
            ok = store.samplePose(c.st->uniqueId, absTick, pose);
        }
        if (!ok)
            continue;
        TriggerWorldStore::writePose(object, pose);
        c.st->dirty = true;
        ++applied;
    }
    // Far / over-budget tracks hold last written pose (or capture baseline).
    simulator.m_triggerWorldAppliedThisTick = true;
}
