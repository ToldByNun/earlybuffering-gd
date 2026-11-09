#include "trigger_bake_job.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../../debug/lag_probe.hpp"
#include "../../../debug/perf_trace.hpp"
#include "../../../level/section_object_walk.hpp"
#include "../trigger_world_baker.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <chrono>
#include <fmt/format.h>

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
            size_t children = 0;
            if (layer && layer->m_objectLayer) {
                if (auto* ch = layer->m_objectLayer->getChildren()) {
                    children = static_cast<size_t>(ch->count());
                }
            }
            bool const hasSections = layer && !layer->m_sections.empty();
            ++owner.m_waitSlices;
            owner.m_progress =
                0.02f *
                std::min(1.f, static_cast<float>(owner.m_waitSlices) /
                                  static_cast<float>(EbTriggerWorldBaker::kMaxWaitObjectSlices));
            if (children > 0 || hasSections) {
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
            if (!layer || !layer->m_objectLayer) {
                owner.m_phase = EbTriggerWorldBaker::Phase::CollectSections;
                owner.m_secOuter = 0;
                owner.m_secInner = 0;
                owner.m_secSlot = 0;
                continue;
            }
            auto* children = layer->m_objectLayer->getChildren();
            if (!children) {
                owner.m_phase = EbTriggerWorldBaker::Phase::CollectSections;
                owner.m_secOuter = 0;
                owner.m_secInner = 0;
                owner.m_secSlot = 0;
                continue;
            }

            // Collect must finish every child across slices (no skip on budget).
            size_t count = 0;
            while (owner.m_childIndex < owner.m_childCount &&
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

            if (owner.m_childIndex >= owner.m_childCount) {
                owner.m_phase = EbTriggerWorldBaker::Phase::CollectSections;
                owner.m_secOuter = 0;
                owner.m_secInner = 0;
                owner.m_secSlot = 0;
            }
            continue;
        }

        if (owner.m_phase == EbTriggerWorldBaker::Phase::CollectSections) {
            auto* layer = owner.m_layer;
            if (!layer) {
                owner.m_phase = EbTriggerWorldBaker::Phase::Bake;
                owner.m_effectIndex = 0;
                continue;
            }

            auto& sections = layer->m_sections;
            size_t count = 0;
            while (count < EbTriggerWorldBaker::kCollectPerSlice && !budgetHit()) {
                if (owner.m_secOuter >= sections.size()) {
                    owner.m_phase = EbTriggerWorldBaker::Phase::Bake;
                    owner.m_effectIndex = 0;
                    if (owner.m_sectionExtras > 0) {
                        EbAssistLog::debug(fmt::format(
                            "TRIGGER-WORLD: section collect +{} objects (total objs={} effects={})",
                            owner.m_sectionExtras, owner.m_objects.size(), owner.m_effects.size()));
                    }
                    break;
                }
                auto* mid = sections[owner.m_secOuter];
                // Never raw typeinfo_cast on section slots — poison / tagged
                // garbage AVs (read @ -1). Same filter as EbSectionObjectWalk.
                if (!EbSectionObjectWalk::pointerLooksLive(mid) ||
                    owner.m_secInner >= mid->size()) {
                    ++owner.m_secOuter;
                    owner.m_secInner = 0;
                    owner.m_secSlot = 0;
                    continue;
                }
                auto* inner = (*mid)[owner.m_secInner];
                if (!EbSectionObjectWalk::pointerLooksLive(inner) ||
                    owner.m_secSlot >= inner->size()) {
                    ++owner.m_secInner;
                    owner.m_secSlot = 0;
                    continue;
                }
                auto* rawData = (*inner)[owner.m_secSlot];
                ++owner.m_secSlot;
                ++count;
                size_t const before = owner.m_seenIds.size();
                owner.considerCollectedObject(EbSectionObjectWalk::asGameObject(rawData));
                if (owner.m_seenIds.size() > before) {
                    ++owner.m_sectionExtras;
                }
            }

            owner.m_progress =
                0.12f + 0.03f * (sections.empty()
                                     ? 1.f
                                     : std::min(1.f, static_cast<float>(owner.m_secOuter) /
                                                         static_cast<float>(sections.size())));
            continue;
        }

        if (owner.m_phase == EbTriggerWorldBaker::Phase::Bake) {
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
