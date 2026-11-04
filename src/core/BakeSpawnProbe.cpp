#include "BakeSpawnProbe.hpp"

#include "SectionObjectWalk.hpp"
#include "ShadowSimulator.hpp"
#include "TriggerWorldStore.hpp"
#include "../config/StageConfig.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>

using namespace geode::prelude;

bool BakeSpawnProbe::isLethalOverlapType(GameObject* obj) {
    if (!obj) return false;
    // Solids at spawn are usually floor / player proxies — not TW movers and not
    // what should force a Trigger World rebuild. Hazards only.
    switch (obj->m_objectType) {
        case GameObjectType::Hazard:
        case GameObjectType::AnimatedHazard:
            return true;
        default:
            return false;
    }
}

bool BakeSpawnProbe::isIgnoredOverlapObject(GameObject* obj) {
    if (!obj) return true;
    if (typeinfo_cast<PlayerObject*>(obj)) return true;
    if (ShadowSimulator::get().isOwnedGhostPtr(obj)) return true;
    // Internal / proxy solids with no editor object id (seen as uid 31.. overlapping
    // the exact player rect) — never real spikes.
    if (obj->m_objectID == 0 && obj->m_objectType == GameObjectType::Solid) {
        return true;
    }
    return false;
}

void BakeSpawnProbe::appendGroupsBrief(GameObject* obj, Hit& hit) {
    hit.groupsBrief.clear();
    (void)obj;
}

void BakeSpawnProbe::fillTwFields(Hit& hit) {
    auto const* track = TriggerWorldStore::get().findTrack(hit.uniqueId);
    if (!track) {
        hit.inTriggerWorld = false;
        hit.twTrackReady = false;
        hit.twKeyCount = 0;
        return;
    }
    hit.inTriggerWorld = true;
    hit.twTrackReady = track->ready && !track->keys.empty();
    hit.twKeyCount = static_cast<int>(track->keys.size());
}

BakeSpawnProbe::Result BakeSpawnProbe::probe(PlayLayer* layer, int startSnapshotSlot) {
    Result out;
    out.startSlot = startSnapshotSlot;
    if (!layer) {
        out.err = "no PlayLayer";
        return out;
    }
    auto& sim = ShadowSimulator::get();
    auto* snap = sim.snapshotSlot(startSnapshotSlot);
    if (!snap) {
        out.err = "missing start snapshot";
        return out;
    }
    out.snapX = snap->getPositionX();
    out.snapY = snap->getPositionY();
    out.snapMode = static_cast<int>(StageConfig::kindOf(snap));
    out.snapIsDart = snap->m_isDart;
    out.jumpHeld = snap->m_holdingButtons[static_cast<int>(PlayerButton::Jump)];
    out.twTracks = static_cast<int>(TriggerWorldStore::get().trackCount());

    PlayerObject* body = sim.shadow();
    if (body) {
        ShadowSimulator::copyPlayerState(body, snap);
    }
    else {
        body = snap;
    }

    auto const pref = body->getObjectRect();
    constexpr int kMaxHits = 16;
    auto consider = [&](GameObject* obj) {
        if (isIgnoredOverlapObject(obj)) return;
        if (!isLethalOverlapType(obj)) return;
        if (static_cast<int>(out.overlaps.size()) >= kMaxHits) return;
        auto const hr = obj->getObjectRect();
        if (!pref.intersectsRect(hr)) return;
        for (auto const& prev : out.overlaps) {
            if (prev.uniqueId == obj->m_uniqueID) return;
        }
        Hit hit;
        hit.uniqueId = obj->m_uniqueID;
        hit.objectId = obj->m_objectID;
        hit.objectType = static_cast<int>(obj->m_objectType);
        hit.x = obj->getPositionX();
        hit.y = obj->getPositionY();
        hit.rectX = hr.origin.x;
        hit.rectY = hr.origin.y;
        hit.rectW = hr.size.width;
        hit.rectH = hr.size.height;
        appendGroupsBrief(obj, hit);
        fillTwFields(hit);
        if (hit.inTriggerWorld) ++out.trackedOverlapCount;
        out.overlaps.push_back(hit);
    };

    if (auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr) {
        for (unsigned i = 0; i < children->count(); ++i) {
            consider(SectionObjectWalk::asGameObject(children->objectAtIndex(i)));
        }
    }
    SectionObjectWalk::forEachInSections(layer, consider);

    out.overlapCount = static_cast<int>(out.overlaps.size());
    // Only untracked hazards should force a TW build for bake.
    out.recommendsTriggerWorld = false;
    for (auto const& h : out.overlaps) {
        if (!h.inTriggerWorld) {
            out.recommendsTriggerWorld = true;
            break;
        }
    }
    out.ok = true;
    return out;
}

std::string BakeSpawnProbe::formatText(Result const& r) {
    std::ostringstream o;
    o << "# BakeSpawnProbe (start-snap HAZARD overlaps before physics)\n";
    o << "ok=" << (r.ok ? "true" : "false") << "\n";
    if (!r.err.empty()) o << "err=" << r.err << "\n";
    o << "startSlot=" << r.startSlot << "\n";
    o << fmt::format(
        "snap=({:.2f},{:.2f}) mode={} isDart={} jumpHeld={}\n",
        r.snapX, r.snapY, r.snapMode, r.snapIsDart ? 1 : 0, r.jumpHeld ? 1 : 0
    );
    o << "twTracks=" << r.twTracks << "\n";
    o << "overlapCount=" << r.overlapCount
      << " trackedOverlapCount=" << r.trackedOverlapCount << "\n";
    o << "recommendsTriggerWorld="
      << (r.recommendsTriggerWorld ? "true" : "false") << "\n";
    for (size_t i = 0; i < r.overlaps.size(); ++i) {
        auto const& h = r.overlaps[i];
        o << fmt::format(
            "  hit[{}] uid={} objId={} type={} @({:.1f},{:.1f}) "
            "rect=({:.1f},{:.1f},{:.1f},{:.1f}) tw={} ready={} keys={} groups=[{}]\n",
            i, h.uniqueId, h.objectId, h.objectType, h.x, h.y,
            h.rectX, h.rectY, h.rectW, h.rectH,
            h.inTriggerWorld ? 1 : 0, h.twTrackReady ? 1 : 0, h.twKeyCount,
            h.groupsBrief
        );
    }
    return o.str();
}

bool BakeSpawnProbe::selfCheck(std::string& report) {
    Result empty;
    empty.ok = true;
    auto const text = formatText(empty);
    if (text.find("overlapCount=0") == std::string::npos) {
        report = "BakeSpawnProbe selfCheck FAIL format";
        return false;
    }
    // Contract with ShadowSimulator spawn grace (jacheronbe frame-5 spike).
    if (!ShadowSimulator::bakeSpawnDeathIgnored(
            5,
            ShadowSimulator::kBakeSpawnGraceFloor,
            ShadowSimulator::kBakeSpawnGraceHardCap,
            true
        )) {
        report = "BakeSpawnProbe selfCheck FAIL spawn-grace-f5";
        return false;
    }
    report = "BakeSpawnProbe selfCheck OK";
    return true;
}
