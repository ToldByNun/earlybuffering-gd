#ifndef SHADOW_WORLD_CAPTURE_HPP
#define SHADOW_WORLD_CAPTURE_HPP

#include "../shadow_simulator.hpp"

class EbShadowWorldCapture {
public:
    static void captureTriggerTransforms(EbShadowSimulator& simulator, PlayLayer* layer);
    static void captureToggleUnknownTargets(EbShadowSimulator& simulator, PlayLayer* layer);
    static void captureDynamicFollows(EbShadowSimulator& simulator, PlayLayer* layer);
    // Touch movers / follow-like static skips that TW does not bake → Hard Unknown
    // targets (otherwise editor pose is treated as Include and Live Delay lies).
    static void captureUnmodeledMotionTargets(EbShadowSimulator& simulator, PlayLayer* layer);
    static void resetTriggerTransforms(EbShadowSimulator& simulator);
    static void restoreTriggerTransforms(EbShadowSimulator& simulator, GJBaseGameLayer* layer);
    static void beginCapture(EbShadowSimulator& simulator, PlayLayer* layer);
    static void resetCaptureForNextRun(EbShadowSimulator& simulator, PlayLayer* layer);
    static void restoreCapture(EbShadowSimulator& simulator, PlayLayer* layer);

private:
    EbShadowWorldCapture() = delete;
};

#endif
