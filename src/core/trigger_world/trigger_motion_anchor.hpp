#ifndef TRIGGER_MOTION_ANCHOR_HPP
#define TRIGGER_MOTION_ANCHOR_HPP

#include "trigger_pose.hpp"

class EbTriggerMotionAnchor {
public:
    static EbTriggerPose project(EbTriggerPose const& live, EbTriggerPose const& reference,
                                 EbTriggerPose const& future);

private:
};

#endif
