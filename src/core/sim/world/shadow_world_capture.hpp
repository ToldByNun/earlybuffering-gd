#ifndef SHADOW_WORLD_CAPTURE_HPP
#define SHADOW_WORLD_CAPTURE_HPP

#include "../shadow_simulator.hpp"

class ShadowWorldCapture {
public:
    static void captureTriggerTransforms(ShadowSimulator& simulator, PlayLayer* layer);
    static void captureToggleUnknownTargets(ShadowSimulator& simulator, PlayLayer* layer);
    static void captureDynamicFollows(ShadowSimulator& simulator, PlayLayer* layer);
    static void resetTriggerTransforms(ShadowSimulator& simulator);
    static void restoreTriggerTransforms(ShadowSimulator& simulator, GJBaseGameLayer* layer);
    static void beginCapture(ShadowSimulator& simulator, PlayLayer* layer);
    static void resetCaptureForNextRun(ShadowSimulator& simulator, PlayLayer* layer);
    static void restoreCapture(ShadowSimulator& simulator, PlayLayer* layer);

private:
    ShadowWorldCapture() = delete;
};

#endif
