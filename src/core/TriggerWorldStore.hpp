#ifndef EARLYBUFFER_CORE_TRIGGERWORLDSTORE_HPP
#define EARLYBUFFER_CORE_TRIGGERWORLDSTORE_HPP

#include "TriggerClassify.hpp"

#include <Geode/Geode.hpp>
#include <cstddef>
#include <string>
#include <unordered_map>
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
// Disk: {saveDir}/triggers/{fullKey}.json — full per-level bake.
// Runtime spool uses only objects currently in objectLayer (GD stream) ∩ index.
class TriggerWorldStore {
public:
    static constexpr int kFileVersion = 1;
    // Soft ceiling against pathological RAM — not a design target. Prefer baking all
    // spoolable Static* targets; raise if a level still truncates.
    static constexpr size_t kMaxTracks = 131072;
    static constexpr size_t kMaxKeysPerTrack = 8192;

    static TriggerWorldStore& get();

    void clear();
    void loadForLevel(GJGameLevel* level);
    void save() const;

    bool loaded() const { return this->m_loaded; }
    bool hasTracks() const { return !this->m_data.tracks.empty(); }
    bool dirty() const { return this->m_dirty; }
    void markDirty() { this->m_dirty = true; }
    // True only after a baker finishOk (or a disk cache load). Cancelled
    // partials are not complete and must not save / soft-trust as spoolable.
    bool bakeComplete() const { return this->m_bakeComplete; }
    void setBakeComplete(bool complete) { this->m_bakeComplete = complete; }
    // Drop tracks but keep levelKey / loaded (cancel mid-build).
    void clearTracks();

    TriggerWorldData const& data() const { return this->m_data; }

    // Replace in-memory data (baker publishPartial). Marks dirty, not complete.
    void setData(TriggerWorldData data);

    TriggerTrack const* findTrack(int uniqueId) const;
    // Ready Static* track present (O(1) index).
    bool hasReadyStaticTrack(int uniqueId) const;
    size_t trackCount() const { return this->m_data.tracks.size(); }
    size_t readyTrackCount() const;

    // Map live attempt tick → store/bake tick using data().tps and liveSimDt.
    // Identity when authorTps * liveSimDt ≈ 1.
    int remapLiveTick(int liveAbsTick) const;
    int remapLiveTick(int liveAbsTick, float liveSimDt) const;

    // Sample at live absTick (remaps to store tick first).
    // Hold last key with tick <= storeTick (baker already steps move mid-keys).
    // Before first key → false = keep baseline; past last → hold last.
    bool samplePose(int uniqueId, int liveAbsTick, TriggerPose& out) const;
    // Sample with an already-remapped store tick (tests / bakers).
    bool samplePoseAtStoreTick(int uniqueId, int storeTick, TriggerPose& out) const;

    // Write a sampled pose onto a live GameObject (position/rot/scale/visible).
    static void writePose(GameObject* obj, TriggerPose const& pose);

    // Apply ready Static* poses for objects currently in objectLayer (near-first).
    // Returns number of objects written. No-op if store empty / incomplete.
    int applyAtTick(PlayLayer* layer, int liveAbsTick);

    std::string statusLine() const;

    // Headless: samplePose hold/before/after, remap insane-snap, writePose(null).
    // Leaves the singleton cleared (safe at mod load before any level).
    static bool selfCheck(std::string& report);

private:
    TriggerWorldStore() = default;

    void rebuildIndex();

    geode::Result<> writeFile() const;
    geode::Result<> readFile();

    static std::filesystem::path triggersDir();
    static std::filesystem::path triggerPath(std::string const& key);

    static char const* kindToString(TriggerKind kind);
    static TriggerKind kindFromString(std::string const& name);

    static void clampData(TriggerWorldData& data);

    TriggerWorldData m_data;
    std::unordered_map<int, size_t> m_index;
    bool m_loaded = false;
    mutable bool m_dirty = false;
    bool m_bakeComplete = false;
};

#endif // EARLYBUFFER_CORE_TRIGGERWORLDSTORE_HPP
