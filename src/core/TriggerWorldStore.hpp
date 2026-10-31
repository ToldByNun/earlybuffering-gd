#ifndef EARLYBUFFER_CORE_TRIGGERWORLDSTORE_HPP
#define EARLYBUFFER_CORE_TRIGGERWORLDSTORE_HPP

#include "TriggerClassify.hpp"

#include <Geode/Geode.hpp>
#include <cstddef>
#include <string>
#include <vector>

class GJGameLevel;
class GameObject;
class PlayLayer;

// One sampled pose for a moved/rotated/scaled object at an absolute attempt tick.
struct TriggerPose {
    int tick = 0;
    float x = 0.f;
    float y = 0.f;
    float rotation = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    bool active = true;
};

// Sparse keyframe track for one GameObject (keyed by m_uniqueID).
struct TriggerTrack {
    int uniqueId = 0;
    int objectId = 0;
    TriggerKind kind = TriggerKind::Ignore;
    bool ready = false;
    std::vector<TriggerPose> keys;
};

struct TriggerWorldData {
    std::string levelKey;
    double tps = 240.0;
    int version = 1;
    std::vector<TriggerTrack> tracks;
};

// Content-keyed cache of static trigger motion.
// Disk: {saveDir}/triggers/{fullKey}.json
// Built by TriggerWorldBaker on cache miss; applied by ShadowSimulator.
class TriggerWorldStore {
public:
    static constexpr int kFileVersion = 1;
    // Soft ceiling: refuse NEW tracks/keys with AssistLog — never silent drop under soft.
    static constexpr size_t kSoftMaxTracks = 100000;
    static constexpr size_t kSoftMaxKeysPerTrack = 32768;
    // Absolute OOM valve only — loud truncate + log (not the day-to-day cap).
    static constexpr size_t kHardMaxTracks = 250000;
    static constexpr size_t kHardMaxKeysPerTrack = 65536;
    // Aliases used by baker / older call sites (= soft).
    static constexpr size_t kMaxTracks = kSoftMaxTracks;
    static constexpr size_t kMaxKeysPerTrack = kSoftMaxKeysPerTrack;

    static TriggerWorldStore& get();

    void clear();
    void loadForLevel(GJGameLevel* level);
    void save() const;

    bool loaded() const { return this->m_loaded; }
    bool hasTracks() const { return !this->m_data.tracks.empty(); }
    bool dirty() const { return this->m_dirty; }
    void markDirty() { this->m_dirty = true; }
    bool softcapHit() const { return this->m_softcapHit; }
    void noteSoftcap(char const* where);

    static bool wouldHitSoftTracks(size_t trackCount) {
        return trackCount >= kSoftMaxTracks;
    }
    static bool wouldHitSoftKeys(size_t keyCount) {
        return keyCount >= kSoftMaxKeysPerTrack;
    }

    TriggerWorldData const& data() const { return this->m_data; }

    // Replace in-memory data (baker). Softcap logs; hard-only truncate.
    void setData(TriggerWorldData data);

    // Keep endpoints + evenly spaced samples (long movers under key soft/hard).
    static void decimateKeys(std::vector<TriggerPose>& keys, size_t maxKeep);

    TriggerTrack const* findTrack(int uniqueId) const;
    size_t trackCount() const { return this->m_data.tracks.size(); }
    size_t readyTrackCount() const;

    // Map live attempt tick → store/bake tick using data().tps and liveSimDt.
    // Identity when authorTps * liveSimDt ≈ 1.
    int remapLiveTick(int liveAbsTick) const;
    int remapLiveTick(int liveAbsTick, float liveSimDt) const;

    // Sample at live absTick (remaps to store tick first).
    // Hold last; before first key → false = keep baseline.
    // Overload with liveSimDt for offline bake (do not use live hitch dt).
    bool samplePose(int uniqueId, int liveAbsTick, TriggerPose& out) const;
    bool samplePose(int uniqueId, int liveAbsTick, float liveSimDt, TriggerPose& out) const;
    // Sample with an already-remapped store tick (tests / bakers).
    bool samplePoseAtStoreTick(int uniqueId, int storeTick, TriggerPose& out) const;

    // Write a sampled pose onto a live GameObject (position/rot/scale/visible).
    static void writePose(GameObject* obj, TriggerPose const& pose);

    // Apply all ready Static* tracks at live absTick (remapped). Walks objectLayer.
    // Returns number of objects written. No-op if store empty / bake off caller.
    int applyAtTick(PlayLayer* layer, int liveAbsTick);

    std::string statusLine() const;

    // Headless: samplePose hold/before/after, writePose(null), kind round-trip.
    // Leaves the singleton cleared (safe at mod load before any level).
    static bool selfCheck(std::string& report);

private:
    TriggerWorldStore() = default;

    geode::Result<> writeFile() const;
    geode::Result<> readFile();

    static std::filesystem::path triggersDir();
    static std::filesystem::path triggerPath(std::string const& key);

    static char const* kindToString(TriggerKind kind);
    static TriggerKind kindFromString(std::string const& name);

    struct ClampResult {
        bool softTracks = false;
        bool softKeys = false;
        bool hardTracks = false;
        bool hardKeys = false;
    };
    static ClampResult clampData(TriggerWorldData& data);

    TriggerWorldData m_data;
    bool m_loaded = false;
    mutable bool m_dirty = false;
    bool m_softcapHit = false;
    mutable bool m_softcapLogged = false;
};

#endif // EARLYBUFFER_CORE_TRIGGERWORLDSTORE_HPP
