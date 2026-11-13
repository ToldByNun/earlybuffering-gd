#include "shadow_section_safety.hpp"
#include "../../../debug/lag_probe.hpp"
#include "../../../debug/perf_trace.hpp"
#include "../orbs/toggle_orb_sim.hpp"
#include "../shadow_simulator.hpp"
#include "shadow_ghost_lifecycle.hpp"
#include <cstdint>

bool EbShadowSectionSafety::tryRemoveFromSection(GJBaseGameLayer* layer, GameObject* object) {
    // Never call GJBaseGameLayer::removeObjectFromSection — it AVs when indices
    // are stale, sections are half-built (init/setupHasCompleted), or mx is bad
    // (crash read @ 0xB8). Erase our pointer from the inner vector ourselves.
    if (!layer || !object)
        return false;
    int const objectX = object->m_outerSectionIndex;
    int const ix = object->m_innerSectionIndex;
    if (objectX < 0 || ix < 0) {
        object->m_outerSectionIndex = -1;
        object->m_innerSectionIndex = -1;
        object->m_middleSectionIndex = -1;
        return false;
    }

    auto& sections = layer->m_sections;
    bool erased = false;
    if (objectX < static_cast<int>(sections.size())) {
        auto* mid = sections[static_cast<size_t>(objectX)];
        if (mid && ix < static_cast<int>(mid->size())) {
            auto* inner = (*mid)[static_cast<size_t>(ix)];
            if (inner) {
                for (int index = static_cast<int>(inner->size()) - 1; index >= 0; --index) {
                    if ((*inner)[static_cast<size_t>(index)] != object)
                        continue;
                    inner->erase(inner->begin() + static_cast<std::ptrdiff_t>(index));
                    erased = true;
                    break;
                }
            }
        }
    }

    object->m_outerSectionIndex = -1;
    object->m_innerSectionIndex = -1;
    object->m_middleSectionIndex = -1;
    return erased;
}

void EbShadowSectionSafety::scrubOwnedGhostsFromSections(GJBaseGameLayer* layer,
                                                       EbShadowSimulator* self) {
    if (!layer || !self)
        return;

    // Only match our known ghost pointers by raw address. Section slots can hold
    // freed/poison entries (0xFFF…); typeinfo_cast on those AVs (read @ -1).
    GameObject* ghosts[1 + EbShadowSimulator::SNAPSHOT_SLOTS + EbShadowSimulator::DEV_EVAL_SLOTS]{};
    int ghostCount = 0;
    if (self->m_shadow) {
        ghosts[ghostCount++] = self->m_shadow;
    }
    for (auto* slot : self->m_snapshots) {
        if (!slot)
            continue;
        ghosts[ghostCount++] = slot;
    }
    for (auto* effectObject : self->m_evalGhosts) {
        if (!effectObject)
            continue;
        ghosts[ghostCount++] = effectObject;
    }
    if (ghostCount <= 0)
        return;

    auto isOwnedSlot = [&](GameObject* object) -> bool {
        if (!object)
            return false;
        auto const addr = reinterpret_cast<uintptr_t>(object);
        // Reject null-page and classic poison (-1) without dereferencing.
        if (addr < 0x10000ull || addr == ~uintptr_t{0})
            return false;
        for (int gameObject = 0; gameObject < ghostCount; ++gameObject) {
            if (ghosts[gameObject] == object)
                return true;
        }
        return false;
    };

    auto scrubCell = [&](auto* inner, int& found) {
        if (!inner || found >= ghostCount)
            return;
        for (int index = static_cast<int>(inner->size()) - 1; index >= 0; --index) {
            if (static_cast<size_t>(index) >= inner->size())
                continue;
            auto* object = (*inner)[static_cast<size_t>(index)];
            if (!isOwnedSlot(object))
                continue;
            inner->erase(inner->begin() + static_cast<std::ptrdiff_t>(index));
            ++found;
            if (found >= ghostCount)
                return;
        }
    };

    auto& sections = layer->m_sections;
    int found = 0;

    // Fast path: only touch cells named by each ghost's section indices.
    for (int gameObject = 0; gameObject < ghostCount; ++gameObject) {
        auto* object = ghosts[gameObject];
        if (!object)
            continue;
        int const objectX = object->m_outerSectionIndex;
        int const ix = object->m_innerSectionIndex;
        int const mx = object->m_middleSectionIndex;
        if (objectX < 0 || ix < 0 || mx < 0)
            continue;
        if (objectX >= static_cast<int>(sections.size()))
            continue;
        auto* mid = sections[static_cast<size_t>(objectX)];
        if (!mid || ix >= static_cast<int>(mid->size()))
            continue;
        scrubCell((*mid)[static_cast<size_t>(ix)], found);
        if (found >= ghostCount)
            return;
    }

    // Slow path only if some ghosts still might linger (bad / -1 indices).
    // Early-exit once every owned pointer was erased — never walk the whole
    // grid "just in case" after that (was ~20ms/restart on large levels).
    if (found >= ghostCount)
        return;

    size_t const outerN = sections.size();
    for (size_t objectX = 0; objectX < outerN; ++objectX) {
        auto* mid = sections[objectX];
        if (!mid)
            continue;
        size_t const midN = mid->size();
        for (size_t ix = 0; ix < midN; ++ix) {
            scrubCell((*mid)[ix], found);
            if (found >= ghostCount)
                return;
        }
    }
}

void EbShadowSectionSafety::evacuateGhosts(EbShadowSimulator& simulator, GJBaseGameLayer* layer) {
    EbPerfTrace::Scope const scope("evacuateGhosts");
    EbLagProbe::Scope const lagScope("eb.evacuateGhosts");
    // Abort any in-flight sim; do NOT restoreCapture here — reset/checkpoint
    // rebuilds object state and restoring into freed/reused objects can AV.
    simulator.m_simulating = false;
    simulator.m_applyingRing = false;
    simulator.m_orbScanning = false;
    simulator.m_orbOverlapTick = false;
    simulator.m_dead = false;
    simulator.m_jumped = false;
    simulator.m_unknown = false;
    simulator.m_toggleUnknownHit = false;
    simulator.m_activatedRing = false;
    simulator.m_ringCount = 0;
    simulator.m_deathObject = nullptr;
    simulator.m_simAbsTick = -1;
    simulator.m_simTickBase = 0;
    simulator.m_poseRemapDt = -1.f;
    simulator.m_triggerWorldAppliedThisTick = false;
    simulator.m_dynamicTargetIds.clear();
    simulator.m_toggleTargetIds.clear();
    simulator.m_dynamicFollowJobs.clear();
    simulator.m_liveDynamicAppliedIds.clear();
    EbToggleOrbSim::get().restore();
    // If a sim aborted after applyAtTick moved solids, put them back before
    // we drop the snapshot — reset/checkpoint must not inherit drifted poses.
    if (!simulator.m_savedTransforms.empty() && layer) {
        EbLagProbe::Scope const state("eb.trigger.restore");
        simulator.restoreTriggerTransforms(layer);
    }
    simulator.m_saved.clear();
    simulator.m_savedTransforms.clear();
    simulator.m_savedActivatedIDs.clear();

    // PlayLayer::init → setupHasCompleted → resetLevel runs BEFORE attach().
    // Touching section vectors or stale ghosts there AVs (GD read @ 0xB8).
    if (!layer || layer != simulator.m_hostLayer || !simulator.m_shadow) {
        EbLagProbe::get().mark("evacuate.skip", "no host / no shadow / wrong layer");
        return;
    }

    // Scrub WHILE indices are still valid. tryRemove clears them to -1 — calling
    // scrub after that forced the slow full-grid walk (~10–20ms on large levels)
    // on every death→reset after assist had simulated.
    bool anyIndexed = false;
    auto noteIndexed = [&](PlayerObject* player) {
        if (!player)
            return;
        if (player->m_outerSectionIndex >= 0 || player->m_innerSectionIndex >= 0 ||
            player->m_middleSectionIndex >= 0) {
            anyIndexed = true;
        }
    };
    noteIndexed(simulator.m_shadow);
    for (auto* slot : simulator.m_snapshots) {
        noteIndexed(slot);
    }
    for (auto* effectObject : simulator.m_evalGhosts) {
        noteIndexed(effectObject);
    }
    EbLagProbe::get().mark("evacuate.indexed", anyIndexed ? "yes" : "no");
    if (anyIndexed) {
        EbLagProbe::Scope const state("eb.evacuate.scrub");
        EbShadowSimulator::scrubOwnedGhostsFromSections(layer, (&simulator));
    }
    // Belt-and-suspenders: index erase if scrub missed a stale cell.
    {
        EbLagProbe::Scope const state("eb.evacuate.tryRemove");
        EbShadowSimulator::tryRemoveFromSection(layer, simulator.m_shadow);
        for (auto* slot : simulator.m_snapshots) {
            EbShadowSimulator::tryRemoveFromSection(layer, slot);
        }
        for (auto* effectObject : simulator.m_evalGhosts) {
            EbShadowSimulator::tryRemoveFromSection(layer, effectObject);
        }
    }

    auto evac = [&](PlayerObject* player) {
        if (!player)
            return;
        player->m_outerSectionIndex = -1;
        player->m_innerSectionIndex = -1;
        player->m_middleSectionIndex = -1;
        player->setPosition({0.f, 105.f});
        player->m_position = cocos2d::CCPoint{0.f, 105.f};
        player->m_yVelocity = 0.0;
        player->m_isDead = false;
        EbShadowGhostLifecycle::clearWaveTrailGeometry(player);
        if (player->m_touchingRings) {
            player->m_touchingRings->removeAllObjects();
        }
        if (player->m_collisionLogTop) {
            simulator.clearCollisionLogs(player);
        }
        simulator.hideGhost(player);
    };

    {
        EbLagProbe::Scope const state("eb.evacuate.park");
        evac(simulator.m_shadow);
        for (auto* slot : simulator.m_snapshots) {
            evac(slot);
        }
        for (auto* effectObject : simulator.m_evalGhosts) {
            evac(effectObject);
        }
    }
    simulator.m_evalActive = nullptr;
    // Cancel in-flight eval loop; next evalDelayFromFreeze clears the flag.
    simulator.m_evalAbort = true;
}
