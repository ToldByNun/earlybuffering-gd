#ifndef TRIGGER_POSE_HPP
#define TRIGGER_POSE_HPP

// One sampled pose for a moved/rotated/scaled object at an absolute attempt tick.
class EbTriggerPose {
public:
    int tick = 0;
    float x = 0.f;
    float y = 0.f;
    float rotation = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    bool active = true;

private:
};

#endif
