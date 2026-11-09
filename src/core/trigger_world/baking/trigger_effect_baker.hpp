#ifndef TRIGGER_EFFECT_BAKER_HPP
#define TRIGGER_EFFECT_BAKER_HPP

#include "../trigger_classify.hpp"
#include "../trigger_world_store.hpp"
#include <vector>

#include "../trigger_world_baker.hpp"

class EbTriggerEffectBaker {
public:
    static EbTriggerWorldBaker::LivePose poseFromObject(GameObject* object);
    static EbTriggerPose toKey(int tick, EbTriggerWorldBaker::LivePose const& live);
    static void pushKey(EbTriggerTrack& track, int tick, EbTriggerWorldBaker::LivePose const& live);
    static int durationTicks(float durationSec, double tps);
    static bool isFollowLikeStatic(EffectGameObject* effect, TriggerKind kind);
    static cocos2d::CCPoint
    groupCenter(int groupId, std::vector<GameObject*> const& objects,
                std::unordered_map<int, EbTriggerWorldBaker::LivePose> const& live);
    static EbTriggerTrack* ensureTrack(EbTriggerWorldBaker& owner, GameObject* object,
                                     TriggerKind kind);
    static std::vector<GameObject*> objectsInGroup(EbTriggerWorldBaker const& owner, int groupId);
    static void bakeOneEffect(EbTriggerWorldBaker& owner, EffectGameObject* effect);

private:
    EbTriggerEffectBaker() = delete;
};

#endif
