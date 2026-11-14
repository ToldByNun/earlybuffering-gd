#include "trigger_motion_anchor.hpp"
#include <cmath>

EbTriggerPose EbTriggerMotionAnchor::project(EbTriggerPose const& live,
                                             EbTriggerPose const& reference,
                                             EbTriggerPose const& future) {
    EbTriggerPose result = future;
    result.x = live.x + future.x - reference.x;
    result.y = live.y + future.y - reference.y;
    result.rotation = live.rotation + future.rotation - reference.rotation;
    result.scaleX = std::fabs(reference.scaleX) > 0.000001f
                        ? live.scaleX * future.scaleX / reference.scaleX
                        : live.scaleX + future.scaleX - reference.scaleX;
    result.scaleY = std::fabs(reference.scaleY) > 0.000001f
                        ? live.scaleY * future.scaleY / reference.scaleY
                        : live.scaleY + future.scaleY - reference.scaleY;
    result.active = future.active == reference.active ? live.active : future.active;
    return result;
}
