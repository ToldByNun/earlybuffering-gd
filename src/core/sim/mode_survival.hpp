#ifndef MODE_SURVIVAL_HPP
#define MODE_SURVIVAL_HPP

#include <string>

// Commit 9 — Lethal vs recoverable vs landing (ideas reimplemented; no upstream code).
// Pure classifiers: cube/ball landing, wave recoverable / flip probes, ship-like recover,
// narrow overlap-ignore, late-crash-after-clear. ParityGate: ascent alone ≠ Landing.
class EbModeSurvival {
public:
    enum class Class {
        Alive = 0,
        Landing,             // cube/ball settled past threat
        Recoverable,         // continuous turn-away / recover after clear
        LateCrashAfterClear, // landed/cleared then died later (not this-click lethal)
        Lethal
    };

    class Input {
    public:
        bool continuous = false;
        bool isWave = false;
        bool isShipLike = false; // ship / ufo / spider / swing
        bool isCubeLike = false; // cube / ball / robot (discrete ground)
        bool upsideDown = false;

        bool died = false;
        bool diedBeforeAction = false;
        bool zoneValid = false;
        bool zoneCleared = false;
        bool groundedSettled = false; // runOne groundedStreak path / ticksBeforeNeutral
        bool activatedRing = false;

        // Narrow only: hazard already overlapping at click (follow-spike / spent).
        bool hazardOverlapAtClick = false;
        bool hazardIsFollowOrSpent = false;

        float deathX = 0.f;
        float zoneEndX = 0.f;
        float playerY = 0.f;
        float deathY = 0.f;
        int deathTick = -1;
        int ticksBeforeNeutral = 0;

    private:
    };

    static char const* classId(Class candidate);

    static Class classify(Input const& in);

    // Soft not-lethal (Landing / Recoverable / LateCrash) — false-lethal cut only.
    // Does NOT mean Live may arm Delay into a death the ghost already recorded.
    static bool countsAsSafeForDelay(Class candidate);

    // Live Delay-arm / pick: only a still-living ghost (Class::Alive).
    static bool countsAsDelayWorthy(Class candidate);

    // Late crash after land → do not invent a Delay opportunity for this click.
    static bool shouldSkipFalseOpportunity(Class candidate);

    // Overlap-ignore: only follow/spent hazards already overlapping at click.
    static bool narrowOverlapIgnore(Input const& in);

    // Wave helpers (pure): 3 flip probes + smallest clear +1px margin.
    static constexpr float kWaveClearMarginPx = 1.f;
    static constexpr int kWaveFlipProbes = 3;
    static bool waveFlipProbeClears(float obstacleX, float const sampleEndX[kWaveFlipProbes],
                                    float marginPx = kWaveClearMarginPx);
    static float clearMarginPx(float obstacleX, float endX);
    static bool waveSmallestClearOk(float clearPx, float marginPx = kWaveClearMarginPx);

    // ParityGate: ascent-shaped death alone must not invent Landing.
    static bool ascentAloneNotLanding(bool died, bool zoneCleared, bool groundedSettled,
                                      bool deathLooksLikeAscent);

    static bool selfCheck(std::string& report);

private:
    EbModeSurvival() = delete;
};

#endif
