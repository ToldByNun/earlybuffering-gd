#ifndef EARLYBUFFER_CORE_TRIGGERWORLDBAKER_HPP
#define EARLYBUFFER_CORE_TRIGGERWORLDBAKER_HPP

#include "TriggerWorldStore.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

class PlayLayer;
class GameObject;
class EffectGameObject;

// Builds static TriggerWorldStore tracks from spawn-timed Move/Rotate/Scale/Toggle.
// Commit 7: budgeted tickSlice on enter (no hitch). cancel() drops in-flight work.
class TriggerWorldBaker {
public:
    static constexpr size_t kCollectPerSlice = 256;
    static constexpr size_t kEffectsPerSlice = 8;
    static constexpr int kBudgetUs = 750;

    static TriggerWorldBaker& get();

    // Queue a cache-miss build. Work runs via tickSlice (physics tick).
    bool start(PlayLayer* layer);
    void cancel();
    bool running() const { return this->m_running; }
    float progress() const { return this->m_progress; }
    std::string statusLine() const;

    // Amortized collect/bake/commit. Safe no-op when idle.
    void tickSlice();

private:
    enum class Phase {
        Idle = 0,
        Collect,
        Bake,
        Commit
    };

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

    std::vector<GameObject*> m_objects;
    std::vector<EffectGameObject*> m_effects;
    std::unordered_map<int, LivePose> m_live;
    std::unordered_map<int, TriggerTrack> m_tracks;

    size_t m_bakedEffects = 0;
    size_t m_skippedFollow = 0;
    size_t m_skippedDynamic = 0;
    size_t m_skippedNonSpawn = 0;
};

#endif // EARLYBUFFER_CORE_TRIGGERWORLDBAKER_HPP
