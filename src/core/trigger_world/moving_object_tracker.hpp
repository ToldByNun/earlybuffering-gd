#ifndef MOVING_OBJECT_TRACKER_HPP
#define MOVING_OBJECT_TRACKER_HPP

#include "trigger_pose.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class GameObject;
class PlayLayer;
class PlayerObject;

// Nova-style moving objects: record what actually moved near the player during
// attempts, then predict from the attempt timeline or straight-line motion
// (≤0.5s). Session-only — cleared on quit (no multi-level RAM retain).
class EbMovingObjectTracker {
public:
    static constexpr size_t kMaxTracks = 512;
    static constexpr size_t kMaxSamplesPerTrack = 256;
    static constexpr float kRecordMarginX = 640.f;
    static constexpr float kMinMovePx = 0.75f;
    static constexpr float kMotionHorizonSec = 0.5f;
    static constexpr float kMatchPx = 24.f;

    static EbMovingObjectTracker& get();

    void clear();
    void onLevelEnter();
    void onLevelQuit();
    void onAttemptReset();

    // Sample nearby solids/hazards that moved this tick into the timeline.
    void onPhysicsTick(PlayLayer* layer, PlayerObject* player, int attemptTick);

    // Prefer timeline sample; else extrapolate last velocity up to 0.5s.
    // sourceTag: "timeline" | "motion" | nullptr if none.
    bool samplePose(int uniqueId, int attemptTick, float simDt, EbTriggerPose& out,
                    char const*& sourceTag) const;

    // uniqueIds with samples/velocity near gx (for shadow capture).
    void collectNearIds(float gx, float marginX, std::vector<int>& out) const;

    size_t trackCount() const {
        return m_tracks.size();
    }
    size_t sampleCount() const {
        return m_totalSamples;
    }
    size_t timelineHits() const {
        return m_timelineHits;
    }
    size_t motionHits() const {
        return m_motionHits;
    }

    std::string statusLine() const;

private:
    friend class EbServiceStore;

    class Sample {
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

    class Track {
    public:
        int uniqueId = 0;
        int objectId = 0;
        std::vector<Sample> samples;
        float lastX = 0.f;
        float lastY = 0.f;
        int lastTick = -1;
        float velX = 0.f;
        float velY = 0.f;
        bool hasVel = false;
        bool seen = false;

    private:
    };

    EbMovingObjectTracker() = default;

    Track* ensureTrack(int uniqueId, int objectId);
    static bool isRecordable(GameObject* object);

    std::unordered_map<int, Track> m_tracks;
    size_t m_totalSamples = 0;
    mutable size_t m_timelineHits = 0;
    mutable size_t m_motionHits = 0;
    int m_attemptIndex = 0;
};

#endif
