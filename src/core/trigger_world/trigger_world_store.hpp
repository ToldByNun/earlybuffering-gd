#ifndef TRIGGER_WORLD_STORE_HPP
#define TRIGGER_WORLD_STORE_HPP

#include "trigger_classify.hpp"
#include "trigger_pose.hpp"
#include <Geode/Geode.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class GJGameLevel;
class GameObject;
class PlayLayer;

// Sparse keyframe track for one GameObject (keyed by m_uniqueID).
class EbTriggerTrack {
public:
    int uniqueId = 0;
    int objectId = 0;
    TriggerKind kind = TriggerKind::Ignore;
    bool ready = false;
    std::vector<EbTriggerPose> keys;

private:
};

class EbTriggerWorldData {
public:
    std::string levelKey;
    double tps = 240.0;
    int version = 1;
    std::vector<EbTriggerTrack> tracks;
    // Full collect produced zero static tracks (not an init race). Survives
    // resetLevel pointer changes and is persisted so kickIfStaleEmpty cannot
    // rebuild 20k+ objects every death / enter.
    bool emptyComplete = false;
    size_t emptyObjectCount = 0;
    bool emptySkipDynamic = false;

private:
};

// Content-keyed cache of static trigger motion.
// Disk: {saveDir}/triggers/{fullKey}.ebtw (legacy .json migrated on load).
// Huge caches are refused at load — Nova-style moving-object timelines cover
// those levels without a multi-100MB parse hitch. Cleared on quit (no retain).
class EbTriggerWorldStore {
public:
    static constexpr int kFileVersion = 2;
    // Sync load larger than this freezes gameplay (218MB JSON ≈ 7s). Skip.
    static constexpr std::uint64_t kMaxSyncLoadBytes = 4ull * 1024ull * 1024ull;
    // Soft ceiling: refuse NEW tracks/keys with AssistLog — never silent drop under soft.
    static constexpr size_t kSoftMaxTracks = 100000;
    static constexpr size_t kSoftMaxKeysPerTrack = 32768;
    // Absolute OOM valve only — loud truncate + log (not the day-to-day cap).
    static constexpr size_t kHardMaxTracks = 250000;
    static constexpr size_t kHardMaxKeysPerTrack = 65536;
    // Aliases used by baker / older call sites (= soft).
    static constexpr size_t kMaxTracks = kSoftMaxTracks;
    static constexpr size_t kMaxKeysPerTrack = kSoftMaxKeysPerTrack;

    static EbTriggerWorldStore& get();

    void clear();
    void loadForLevel(GJGameLevel* level);
    void save() const;

    // Huge-disk / oversized level: skip full TW bake; MovingObjectTracker owns poses.
    bool prefersMovers() const {
        return this->m_prefersMovers;
    }
    void setPrefersMovers(bool on) {
        this->m_prefersMovers = on;
    }

    bool loaded() const {
        return this->m_loaded;
    }
    bool hasTracks() const {
        return !this->m_data.tracks.empty();
    }
    // Tracks present OR a verified complete-empty bake for this level.
    // prefersMovers is also resolved — full TW intentionally skipped (Nova path).
    bool resolved() const {
        return this->hasTracks() || this->m_data.emptyComplete || this->m_prefersMovers;
    }
    // Like resolved(), but rebuild when skip-dynamic setting changed since bake.
    bool resolvedForSkipDynamic(bool skipDynamic) const {
        if (this->hasTracks() || this->m_prefersMovers)
            return true;
        return this->m_data.emptyComplete && this->m_data.emptySkipDynamic == skipDynamic;
    }
    bool emptyComplete() const {
        return this->m_data.emptyComplete;
    }
    size_t emptyObjectCount() const {
        return this->m_data.emptyObjectCount;
    }
    bool dirty() const {
        return this->m_dirty;
    }
    void markDirty() {
        this->m_dirty = true;
    }
    // Record a finished collect that yielded no static tracks.
    void markEmptyComplete(size_t objectCount, bool skipDynamic);
    bool softcapHit() const {
        return this->m_softcapHit;
    }
    void noteSoftcap(char const* where);

    static bool wouldHitSoftTracks(size_t trackCount) {
        return trackCount >= kSoftMaxTracks;
    }
    static bool wouldHitSoftKeys(size_t keyCount) {
        return keyCount >= kSoftMaxKeysPerTrack;
    }

    EbTriggerWorldData const& data() const {
        return this->m_data;
    }

    // Replace in-memory data (baker). Softcap logs; hard-only truncate.
    void setData(EbTriggerWorldData data);
    // Commit 4: empty incomplete collect must not poison the on-disk cache.
    void clearDirty() {
        this->m_dirty = false;
    }
    // Delete triggers/<key>.ebtw/.json if present (empty / stale cache).
    void discardCacheFile() const;

    // Keep endpoints + evenly spaced samples (long movers under key soft/hard).
    static void decimateKeys(std::vector<EbTriggerPose>& keys, size_t maxKeep);

    EbTriggerTrack const* findTrack(int uniqueId) const;
    size_t trackCount() const {
        return this->m_data.tracks.size();
    }
    size_t readyTrackCount() const;

    // Map live attempt tick → store/bake tick using data().tps and liveSimDt.
    // Identity when authorTps * liveSimDt ≈ 1.
    int remapLiveTick(int liveAbsTick) const;
    int remapLiveTick(int liveAbsTick, float liveSimDt) const;

    // Sample at live absTick (remaps to store tick first).
    // Hold last; before first key → false = keep baseline.
    // Overload with liveSimDt for offline bake (do not use live hitch dt).
    bool samplePose(int uniqueId, int liveAbsTick, EbTriggerPose& out) const;
    bool samplePose(int uniqueId, int liveAbsTick, float liveSimDt, EbTriggerPose& out) const;
    // Sample with an already-remapped store tick (tests / bakers).
    bool samplePoseAtStoreTick(int uniqueId, int storeTick, EbTriggerPose& out) const;
    static bool sampleTrackAtStoreTick(EbTriggerTrack const& track, int storeTick,
                                       EbTriggerPose& out);

    // Write a sampled pose onto a live GameObject (position/rot/scale/visible).
    static void writePose(GameObject* object, EbTriggerPose const& pose);
    // True when object already matches pose (no Cocos writes needed).
    static bool poseMatchesObject(GameObject* object, EbTriggerPose const& pose);
    // writePose only when poseMatchesObject is false. Returns true if written.
    static bool writePoseIfChanged(GameObject* object, EbTriggerPose const& pose);

    // Apply all ready Static* tracks at live absTick (remapped). Walks objectLayer.
    // Returns number of objects written. No-op if store empty / bake off caller.
    int applyAtTick(PlayLayer* layer, int liveAbsTick);

    std::string statusLine() const;

    // Headless: samplePose hold/before/after, writePose(null), kind round-trip.
    // Leaves the singleton cleared (safe at mod load before any level).
    static bool selfCheck(std::string& report);

private:
    friend class EbServiceStore;

    friend class EbTriggerWorldChecks;

    friend class EbTriggerWorldCodec;

    EbTriggerWorldStore() = default;

    geode::Result<> writeFile() const;
    geode::Result<> readFile();

    static std::filesystem::path triggersDir();
    static std::filesystem::path triggerPath(std::string const& key);

    static char const* kindToString(TriggerKind kind);
    static TriggerKind kindFromString(std::string const& name);

    class ClampResult {
    public:
        bool softTracks = false;
        bool softKeys = false;
        bool hardTracks = false;
        bool hardKeys = false;

    private:
    };
    static ClampResult clampData(EbTriggerWorldData& data);

    EbTriggerWorldData m_data;
    mutable std::unordered_map<int, size_t> m_trackIndex;
    mutable bool m_trackIndexValid = false;
    bool m_loaded = false;
    mutable bool m_dirty = false;
    bool m_softcapHit = false;
    mutable bool m_softcapLogged = false;
    bool m_prefersMovers = false;
};

#endif
