#ifndef TRIGGER_EFFECT_BAKER_HPP
#define TRIGGER_EFFECT_BAKER_HPP

#include "../trigger_classify.hpp"
#include "../trigger_world_store.hpp"
#include <vector>

#include "../trigger_world_baker.hpp"

class TriggerEffectBaker {
public:
    static TriggerWorldBaker::LivePose poseFromObject(GameObject* object);
    static TriggerPose toKey(int tick, TriggerWorldBaker::LivePose const& live);
    static void pushKey(TriggerTrack& track, int tick, TriggerWorldBaker::LivePose const& live);
    static int durationTicks(float durationSec, double tps);
    static bool isFollowLikeStatic(EffectGameObject* effect, TriggerKind kind);
    static cocos2d::CCPoint
    groupCenter(int groupId, std::vector<GameObject*> const& objects,
                std::unordered_map<int, TriggerWorldBaker::LivePose> const& live);
    static TriggerTrack* ensureTrack(TriggerWorldBaker& owner, GameObject* object,
                                     TriggerKind kind);
    static std::vector<GameObject*> objectsInGroup(TriggerWorldBaker const& owner, int groupId);
    static void bakeOneEffect(TriggerWorldBaker& owner, EffectGameObject* effect);

private:
    TriggerEffectBaker() = delete;
};

#endif
