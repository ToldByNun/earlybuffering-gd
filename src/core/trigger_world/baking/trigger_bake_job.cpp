#include "trigger_bake_job.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../../debug/lag_probe.hpp"
#include "../../../debug/perf_trace.hpp"
#include "../../level/section_object_walk.hpp"
#include "../trigger_world_baker.hpp"
#include "trigger_effect_baker.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <chrono>
#include <fmt/format.h>
#include <unordered_map>

void EbTriggerBakeJob::tickSlice(EbTriggerWorldBaker& owner) {
    if (!owner.m_running)
        return;
    if (owner.m_cancel) {
        owner.finishCancel();
        return;
    }

    EbPerfTrace::Scope const scope("triggerBake.slice");
    EbLagProbe::Scope const lagScope("eb.triggerBake.build");

    auto const wall0 = std::chrono::steady_clock::now();
    auto budgetHit = [&]() -> bool {
        auto const elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                             std::chrono::steady_clock::now() - wall0)
                                             .count();
        return elapsedMicroseconds >= EbTriggerWorldBaker::kBudgetUs;
    };

    while (!budgetHit()) {
        if (owner.m_cancel) {
            owner.finishCancel();
            return;
        }

        if (owner.m_phase == EbTriggerWorldBaker::Phase::WaitObjects) {
            auto* layer = owner.m_layer;
            auto* objects = EbSectionObjectWalk::objectArray(layer);
            size_t const children = objects ? objects->count() : 0;
            ++owner.m_waitSlices;
            owner.m_progress =
                0.02f *
                std::min(1.f, static_cast<float>(owner.m_waitSlices) /
                                  static_cast<float>(EbTriggerWorldBaker::kMaxWaitObjectSlices));
            if (children > 0) {
                owner.m_childCount = children;
                owner.m_childIndex = 0;
                owner.m_objects.clear();
                owner.m_effects.clear();
                owner.m_seenIds.clear();
                if (children > 0) {
                    owner.m_objects.reserve(children);
                    owner.m_effects.reserve(256);
                }
                owner.m_phase = EbTriggerWorldBaker::Phase::Collect;
                EbAssistLog::debug(
                    fmt::format("TRIGGER-WORLD: objects ready ({} children) after {} wait slices",
                                children, owner.m_waitSlices));
                continue;
            }
            if (owner.m_waitSlices >= EbTriggerWorldBaker::kMaxWaitObjectSlices) {
                owner.m_collectIncomplete = true;
                owner.m_phase = EbTriggerWorldBaker::Phase::Commit;
                EbAssistLog::info(
                    "TRIGGER-WORLD: WaitObjects timed out — committing incomplete empty");
                continue;
            }
            break; // yield slice; try next physics tick
        }

        if (owner.m_phase == EbTriggerWorldBaker::Phase::Collect) {
            auto* layer = owner.m_layer;
            auto* children = EbSectionObjectWalk::objectArray(layer);
            if (!children) {
                owner.m_collectIncomplete = true;
                owner.m_phase = EbTriggerWorldBaker::Phase::CollectSections;
                continue;
            }
            if (!layer->m_objects || children->count() < owner.m_childCount)
                owner.m_collectIncomplete = true;
            size_t const available = std::min(owner.m_childCount, size_t(children->count()));

            // Collect must finish every child across slices (no skip on budget).
            size_t count = 0;
            while (owner.m_childIndex < available &&
                   count < EbTriggerWorldBaker::kCollectPerSlice && !budgetHit()) {
                auto* object = geode::prelude::typeinfo_cast<GameObject*>(
                    children->objectAtIndex(static_cast<unsigned>(owner.m_childIndex)));
                ++owner.m_childIndex;
                ++count;
                owner.considerCollectedObject(object);
            }

            float const collectFrac = owner.m_childCount > 0
                                          ? static_cast<float>(owner.m_childIndex) /
                                                static_cast<float>(owner.m_childCount)
                                          : 1.f;
            owner.m_progress = 0.12f * collectFrac;

            if (owner.m_childIndex >= available) {
                owner.m_phase = EbTriggerWorldBaker::Phase::CollectSections;
                owner.m_secOuter = 0;
                owner.m_secInner = 0;
                owner.m_secSlot = 0;
            }
            continue;
        }

        if (owner.m_phase == EbTriggerWorldBaker::Phase::CollectSections) {
            // m_objects already owns the level objects, including objects absent
            // from the visible object layer. Never cast non-owning section slots.
            owner.m_phase = EbTriggerWorldBaker::Phase::Bake;
            owner.m_effectIndex = 0;
            continue;
        }

        if (owner.m_phase == EbTriggerWorldBaker::Phase::Bake) {
            if (!owner.m_effectsSorted) {
                std::unordered_map<EffectGameObject*, int> activationTicks;
                activationTicks.reserve(owner.m_effects.size());
                for (auto* effect : owner.m_effects)
                    activationTicks.emplace(effect, EbTriggerEffectBaker::activationTick(
                                                        owner.m_layer, effect, owner.m_tps));
                std::stable_sort(owner.m_effects.begin(), owner.m_effects.end(),
                                 [&](EffectGameObject* left, EffectGameObject* right) {
                                     return activationTicks.at(left) < activationTicks.at(right);
                                 });
                owner.m_effectsSorted = true;
            }

            size_t count = 0;
            while (owner.m_effectIndex < owner.m_effects.size() &&
                   count < EbTriggerWorldBaker::kEffectsPerSlice && !budgetHit()) {
                owner.bakeOneEffect(owner.m_effects[owner.m_effectIndex]);
                ++owner.m_effectIndex;
                ++count;
            }

            float const bakeFrac = owner.m_effects.empty()
                                       ? 1.f
                                       : static_cast<float>(owner.m_effectIndex) /
                                             static_cast<float>(owner.m_effects.size());
            owner.m_progress = 0.15f + 0.80f * bakeFrac;

            if (count > 0) {
                owner.publishPartial();
            }

            if (owner.m_effectIndex >= owner.m_effects.size()) {
                owner.m_phase = EbTriggerWorldBaker::Phase::Commit;
            }
            continue;
        }

        if (owner.m_phase == EbTriggerWorldBaker::Phase::Commit) {
            owner.publishPartial();
            owner.m_phase = EbTriggerWorldBaker::Phase::Idle;
            owner.finishOk();
            return;
        }

        break;
    }
}
