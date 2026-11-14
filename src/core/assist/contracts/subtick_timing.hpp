#ifndef SUBTICK_TIMING_HPP
#define SUBTICK_TIMING_HPP

class EbSubtickTiming {
public:
    static bool withinWindow(int delay, float phase, int limit);
    static int ufoBoundaryDelay(int idleDeathTick, int limit);
    static float probePhase(int index);
    static float segmentPrefix(float phase, float fullStep, float elapsed, float segment);

private:
};

#endif
