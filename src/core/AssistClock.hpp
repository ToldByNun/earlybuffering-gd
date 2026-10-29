#ifndef ASSIST_CLOCK_HPP
#define ASSIST_CLOCK_HPP

#include <string>

// Single physics-quantum clock for assist / bake / trigger-world.
// Live planning uses simDt = lastDt / timeWarp; author TPS must match 1/simDt,
// never warped 1/lastDt (MegaHack-style speed without timeWarp).
class AssistClock {
public:
    static constexpr double kNominalTps = 240.0;
    // |simTps − 240| above this with timeWarp ≈ 1 → speedhack-scale suspect.
    static constexpr double kSuspectAbsDelta = 40.0;
    static constexpr float kWarpNearOne = 0.02f;
    // Throttle suspect logs to once per this many attempt ticks while sticky.
    static constexpr int kSuspectLogEveryTicks = 240;

    static AssistClock& get();

    // Physics TPS after portal timeWarp strip (plan / bake / trigger author).
    double simTpsFromDt(float simDt) const;
    // Wall / raw update TPS (debug / overlay only — never author keys).
    double wallTpsFromDt(float lastDt) const;

    // Map live absolute attempt tick → author/bake tick.
    // bakeTick ≈ round(liveAbsTick * (authorTps * liveSimDt)); identity when
    // authorTps * liveSimDt ≈ 1.
    int remapTick(double authorTps, int liveAbsTick, float liveSimDt) const;

    // True when warp is ~1 but sim TPS is far from nominal (MH dt scale).
    bool speedScaleSuspect(float timeWarp, double simTps) const;

    // Per physics tick: update sticky suspect + return true when a log line
    // should be emitted (edge into suspect, or every kSuspectLogEveryTicks).
    bool noteSpeedScale(float timeWarp, double simTps, int attemptTick);

    bool lastSuspect() const { return this->m_lastSuspect; }

    static bool selfCheck(std::string& report);

private:
    AssistClock() = default;

    int m_lastSuspectLogTick = -1000000;
    bool m_lastSuspect = false;
};

#endif // ASSIST_CLOCK_HPP
