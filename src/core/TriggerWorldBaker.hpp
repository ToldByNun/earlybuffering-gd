#ifndef EARLYBUFFER_CORE_TRIGGERWORLDBAKER_HPP
#define EARLYBUFFER_CORE_TRIGGERWORLDBAKER_HPP

#include "TriggerWorldStore.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class PlayLayer;
class GameObject;
class EffectGameObject;

// Builds static TriggerWorldStore tracks from spawn-timed Move/Rotate/Scale/Toggle.
// Budgeted tickSlice on enter (no hitch). cancel() drops in-flight work.
class TriggerWorldBaker {
public:
    static constexpr size_t kCollectPerSlice = 256;
    static constexpr size_t kEffectsPerSlice = 8;
    static constexpr int kBudgetUs = 750;

    static TriggerWorldBaker& get();

    // onLevelInit: load cache; queue budgeted build on miss. No-op if bake off.
    void loadOrBuild(PlayLayer* layer);
    // Queue a cache-miss build. Work runs via tickSlice (physics tick).
    bool start(PlayLayer* layer);
    void cancel();
    bool running() const { return this->m_running; }
    float progress() const { return this->m_progress; }
    std::string statusLine() const;

    // Last finished bake counters (survive clearJob for Dev witness / overlay).
    size_t lastBakedEffects() const { return this->m_lastBakedEffects; }
    size_t lastSkippedFollow() const { return this->m_lastSkippedFollow; }
    size_t lastSkippedDynamic() const { return this->m_lastSkippedDynamic; }
    size_t lastSkippedNonSpawn() const { return this->m_lastSkippedNonSpawn; }
    size_t lastSkippedTouch() const { return this->m_lastSkippedTouch; }
    size_t lastBakedStartTimed() const { return this->m_lastBakedStartTimed; }
    size_t lastChildCount() const { return this->m_lastChildCount; }
    size_t lastEffectCount() const { return this->m_lastEffectCount; }
    size_t lastSectionExtras() const { return this->m_lastSectionExtras; }
    bool lastCollectIncomplete() const { return this->m_lastCollectIncomplete; }

    // Amortized collect/bake/commit. Safe no-op when idle.
    void tickSlice();
    // After death-cancel: resume build if store still empty (reset path).
    void resumeIfEmpty(PlayLayer* layer);
    // Physics tick: objects appeared after init race → rebuild empty TW.
    void kickIfStaleEmpty(PlayLayer* layer);
    // Bake validate: drain slices until idle or wallMs. err on cancel / incomplete.
    // forceForBake: build/load even when Trigger World setting is off (spawn movers).
    bool waitUntilReady(
        PlayLayer* layer, int wallMs, std::string& err, bool forceForBake = false
);
    // Cache load + start when setting is off (bake-only movers). No-op if tracks exist.
    bool beginForceBuild(PlayLayer* layer);

private:
    enum class Phase {
        Idle = 0,
        WaitObjects, // objectLayer may be empty at onLevelInit
        Collect,
        CollectSections, // secondary: section lists may hold objects children miss
        Bake,
        Commit
    };

    static constexpr int kMaxWaitObjectSlices = 180;

    struct LivePose {
        float x = 0.f;
        float y = 0.f;
        float rotation = 0.f;
        float scaleX = 1.f;
        float scaleY = 1.f;
        bool active = true;
        int objectId = 0;
    };

    TriggerWorldBaker() = default;

    bool cancelled() const { return this->m_cancel; }
    void clearJob();
    void finishCancel();
    void finishOk();
    void publishPartial();
    void bakeOneEffect(EffectGameObject* effect);

    TriggerTrack* ensureTrack(GameObject* obj, TriggerKind kind);
    std::vector<GameObject*> objectsInGroup(int groupId) const;
    void considerCollectedObject(GameObject* obj);

    static LivePose poseFromObject(GameObject* obj);
    static TriggerPose toKey(int tick, LivePose const& live);
    static void pushKey(TriggerTrack& track, int tick, LivePose const& live);
    static int durationTicks(float durationSec, double tps);
    static bool isFollowLikeStatic(EffectGameObject* effect, TriggerKind kind);
    static cocos2d::CCPoint groupCenter(
        int groupId,
        std::vector<GameObject*> const& objects,
        std::unordered_map<int, LivePose> const& live
);

    bool startInternal(PlayLayer* layer, bool forceForBake);

    bool m_running = false;
    bool m_cancel = false;
    float m_progress = 0.f;
    std::string m_status;
    Phase m_phase = Phase::Idle;

    PlayLayer* m_layer = nullptr;
    double m_tps = 240.0;
    std::string m_levelKey;
    size_t m_childIndex = 0;
    size_t m_childCount = 0;
    size_t m_effectIndex = 0;
    size_t m_secOuter = 0;
    size_t m_secInner = 0;
    size_t m_secSlot = 0;
    size_t m_sectionExtras = 0;

    std::vector<GameObject*> m_objects;
    std::vector<EffectGameObject*> m_effects;
    std::unordered_set<int> m_seenIds;
    // Section cells can contain the same raw object pointer many times.
    // De-duplicate before the defensive RTTI probe, not only by uniqueId after it.
    std::unordered_set<void*> m_seenSectionRaw;
    std::unordered_map<int, LivePose> m_live;
    std::unordered_map<int, TriggerTrack> m_tracks;

    size_t m_bakedEffects = 0;
    size_t m_bakedStartTimed = 0; // non-spawn static baked at t=0
    size_t m_skippedFollow = 0;
    size_t m_skippedDynamic = 0;
    size_t m_skippedNonSpawn = 0; // touch-triggered only (legacy name)
    size_t m_skippedTouch = 0;
    size_t m_skippedIgnore = 0; // pulse/camera/etc — not static movers
    size_t m_skippedSpawnTrig = 0; // Spawn trigger objects (not Move targets)
    size_t m_staticMoverSeen = 0; // Move/Rotate/Scale/Toggle considered
    size_t m_staticMoverMissed = 0; // no group / empty targets / bad scale
    int m_waitSlices = 0;
    bool m_collectIncomplete = false;

    size_t m_lastBakedEffects = 0;
    size_t m_lastBakedStartTimed = 0;
    size_t m_lastSkippedFollow = 0;
    size_t m_lastSkippedDynamic = 0;
    size_t m_lastSkippedNonSpawn = 0;
    size_t m_lastSkippedTouch = 0;
    size_t m_lastSkippedIgnore = 0;
    size_t m_lastSkippedSpawnTrig = 0;
    size_t m_lastStaticMoverSeen = 0;
    size_t m_lastStaticMoverMissed = 0;
    size_t m_lastChildCount = 0;
    size_t m_lastEffectCount = 0;
    size_t m_lastSectionExtras = 0;
    bool m_lastCollectIncomplete = false;
};

#endif // EARLYBUFFER_CORE_TRIGGERWORLDBAKER_HPP
