#ifndef EARLYBUFFER_CORE_TRIGGERWORLDBAKER_HPP
#define EARLYBUFFER_CORE_TRIGGERWORLDBAKER_HPP

#include "TriggerWorldStore.hpp"

#include <string>
#include <unordered_map>
#include <vector>

class PlayLayer;
class GameObject;
class EffectGameObject;

// Builds static TriggerWorldStore tracks from spawn-timed Move/Rotate/Scale/Toggle.
// Commit 6: synchronous kinematic build (+ cancel). Commit 7 adds budgeted tickSlice.
class TriggerWorldBaker {
public:
    static TriggerWorldBaker& get();

    // Cache-miss build. Returns false if skipped (busy / bake off / no layer).
    bool start(PlayLayer* layer);
    void cancel();
    bool running() const { return this->m_running; }
    float progress() const { return this->m_progress; }
    std::string statusLine() const;

    // Commit 7: amortized slices. Commit 6 no-op (work finishes in start).
    void tickSlice();

private:
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

    void buildSync(PlayLayer* layer);
    bool cancelled() const { return this->m_cancel; }

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
};

#endif // EARLYBUFFER_CORE_TRIGGERWORLDBAKER_HPP
