#include "moving_object_tracker.hpp"
#include "../../app/class_manager.hpp"
#include "../../debug/assist_log.hpp"
#include "../level/section_object_walk.hpp"
#include "../sim/sim_collision_policy.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

EbMovingObjectTracker& EbMovingObjectTracker::get() {
    return EbClassManager::get().service<EbMovingObjectTracker>();
}

void EbMovingObjectTracker::clear() {
    this->m_tracks.clear();
    this->m_totalSamples = 0;
    this->m_timelineHits = 0;
    this->m_motionHits = 0;
    this->m_attemptIndex = 0;
}

void EbMovingObjectTracker::onLevelEnter() {
    this->clear();
}

void EbMovingObjectTracker::onLevelQuit() {
    this->clear();
}

void EbMovingObjectTracker::onAttemptReset() {
    ++this->m_attemptIndex;
    // Keep timelines across attempts (Nova: "from earlier attempts").
    // Drop per-tick velocity so a respawn does not carry a stale step.
    for (auto& entry : this->m_tracks) {
        entry.second.hasVel = false;
        entry.second.lastTick = -1;
        entry.second.seen = false;
    }
}

bool EbMovingObjectTracker::isRecordable(GameObject* object) {
    if (!object)
        return false;
    if (geode::prelude::typeinfo_cast<PlayerObject*>(object))
        return false;
    // Hazards / solids that can kill — same spirit as Nova's blocks/spikes.
    auto const unknown = EbSimCollisionPolicy::unknownClass(object);
    if (unknown == SimUnknownClass::Hard)
        return true;
    if (object->m_objectType == GameObjectType::Solid ||
        object->m_objectType == GameObjectType::Hazard ||
        object->m_objectType == GameObjectType::AnimatedHazard)
        return true;
    return false;
}

EbMovingObjectTracker::Track* EbMovingObjectTracker::ensureTrack(int uniqueId, int objectId) {
    if (uniqueId == 0)
        return nullptr;
    auto found = this->m_tracks.find(uniqueId);
    if (found != this->m_tracks.end())
        return &found->second;
    if (this->m_tracks.size() >= kMaxTracks)
        return nullptr;
    Track track;
    track.uniqueId = uniqueId;
    track.objectId = objectId;
    auto [ins, ok] = this->m_tracks.emplace(uniqueId, std::move(track));
    if (!ok)
        return nullptr;
    return &ins->second;
}

void EbMovingObjectTracker::onPhysicsTick(PlayLayer* layer, PlayerObject* player, int attemptTick) {
    if (!layer || !player || attemptTick < 0)
        return;

    float const px = player->getPositionX();
    auto* objects = EbSectionObjectWalk::objectArray(layer);
    if (!objects)
        return;

    // Spread scan across ticks — full 20k walks every physics frame would hitch.
    unsigned const count = objects->count();
    unsigned const stride = 8;
    unsigned const phase = static_cast<unsigned>(attemptTick) % stride;
    for (unsigned index = phase; index < count; index += stride) {
        auto* object = EbSectionObjectWalk::asGameObject(objects->objectAtIndex(index));
        if (!isRecordable(object))
            continue;
        float const ox = object->getPositionX();
        if (std::fabs(ox - px) > kRecordMarginX)
            continue;

        auto* track = this->ensureTrack(object->m_uniqueID, object->m_objectID);
        if (!track)
            continue;

        float const oy = object->getPositionY();
        float const rot = object->getRotation();
        float const sx = object->getScaleX();
        float const sy = object->getScaleY();
        bool const active = object->isVisible();

        if (track->lastTick >= 0) {
            int const dticks = attemptTick - track->lastTick;
            if (dticks > 0) {
                float const dx = ox - track->lastX;
                float const dy = oy - track->lastY;
                track->velX = dx / static_cast<float>(dticks);
                track->velY = dy / static_cast<float>(dticks);
                track->hasVel = std::fabs(dx) >= kMinMovePx || std::fabs(dy) >= kMinMovePx;
            }
        }

        bool const moved = !track->seen || std::fabs(ox - track->lastX) >= kMinMovePx ||
                           std::fabs(oy - track->lastY) >= kMinMovePx ||
                           std::fabs(rot - (track->samples.empty() ? rot : track->samples.back().rotation)) >=
                               0.5f ||
                           active != (track->samples.empty() ? active : track->samples.back().active);

        track->lastX = ox;
        track->lastY = oy;
        track->lastTick = attemptTick;
        track->seen = true;

        if (!moved)
            continue;
        if (!track->samples.empty() && track->samples.back().tick == attemptTick) {
            auto& sample = track->samples.back();
            sample.x = ox;
            sample.y = oy;
            sample.rotation = rot;
            sample.scaleX = sx;
            sample.scaleY = sy;
            sample.active = active;
            continue;
        }
        if (track->samples.size() >= kMaxSamplesPerTrack) {
            // Drop oldest half — keep recent attempt motion dense.
            size_t const keep = kMaxSamplesPerTrack / 2;
            track->samples.erase(track->samples.begin(),
                                 track->samples.begin() +
                                     static_cast<std::ptrdiff_t>(track->samples.size() - keep));
        }
        Sample sample;
        sample.tick = attemptTick;
        sample.x = ox;
        sample.y = oy;
        sample.rotation = rot;
        sample.scaleX = sx;
        sample.scaleY = sy;
        sample.active = active;
        track->samples.push_back(sample);
        ++this->m_totalSamples;
    }
}

bool EbMovingObjectTracker::samplePose(int uniqueId, int attemptTick, float simDt,
                                       EbTriggerPose& out, char const*& sourceTag) const {
    sourceTag = nullptr;
    auto found = this->m_tracks.find(uniqueId);
    if (found == this->m_tracks.end())
        return false;
    auto const& track = found->second;

    if (!track.samples.empty()) {
        // Hold last sample at/before tick (Nova timeline).
        auto const after =
            std::upper_bound(track.samples.begin(), track.samples.end(), attemptTick,
                             [](int tick, Sample const& sample) { return tick < sample.tick; });
        if (after != track.samples.begin()) {
            auto const left = std::prev(after);
            out.tick = left->tick;
            out.x = left->x;
            out.y = left->y;
            out.rotation = left->rotation;
            out.scaleX = left->scaleX;
            out.scaleY = left->scaleY;
            out.active = left->active;
            if (after != track.samples.end() && after->tick > left->tick) {
                int const span = after->tick - left->tick;
                float const u = std::clamp(static_cast<float>(attemptTick - left->tick) /
                                               static_cast<float>(span),
                                           0.f, 1.f);
                out.x = left->x + (after->x - left->x) * u;
                out.y = left->y + (after->y - left->y) * u;
                out.rotation = left->rotation + (after->rotation - left->rotation) * u;
                out.scaleX = left->scaleX + (after->scaleX - left->scaleX) * u;
                out.scaleY = left->scaleY + (after->scaleY - left->scaleY) * u;
            }
            sourceTag = "timeline";
            ++this->m_timelineHits;
            return true;
        }
    }

    // Nova: "guessed from its motion at your click (straight on, 0.5 s at most)"
    if (!track.hasVel || track.lastTick < 0)
        return false;
    float const dt = simDt > 0.f ? simDt : (1.f / 240.f);
    int const maxTicks =
        std::max(1, static_cast<int>(std::lround(kMotionHorizonSec / dt)));
    int const delta = attemptTick - track.lastTick;
    if (delta < 0 || delta > maxTicks)
        return false;
    out.tick = attemptTick;
    out.x = track.lastX + track.velX * static_cast<float>(delta);
    out.y = track.lastY + track.velY * static_cast<float>(delta);
    out.rotation = track.samples.empty() ? 0.f : track.samples.back().rotation;
    out.scaleX = track.samples.empty() ? 1.f : track.samples.back().scaleX;
    out.scaleY = track.samples.empty() ? 1.f : track.samples.back().scaleY;
    out.active = track.samples.empty() ? true : track.samples.back().active;
    sourceTag = "motion";
    ++this->m_motionHits;
    return true;
}

void EbMovingObjectTracker::collectNearIds(float gx, float marginX,
                                           std::vector<int>& out) const {
    out.clear();
    out.reserve(std::min(this->m_tracks.size(), static_cast<size_t>(128)));
    for (auto const& entry : this->m_tracks) {
        auto const& track = entry.second;
        float x = track.lastX;
        if (!track.samples.empty())
            x = track.samples.back().x;
        if (std::fabs(x - gx) > marginX)
            continue;
        out.push_back(track.uniqueId);
    }
}

std::string EbMovingObjectTracker::statusLine() const {
    return fmt::format("movers: {} tracks {} samples (timeline={} motion={})", this->m_tracks.size(),
                       this->m_totalSamples, this->m_timelineHits, this->m_motionHits);
}
