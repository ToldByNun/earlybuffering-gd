#ifndef FRIRECRACKER_CORE_LEVELPROFILE_HPP
#define FRIRECRACKER_CORE_LEVELPROFILE_HPP

#include "../config/StageConfig.hpp"

#include <Geode/Geode.hpp>
#include <string>
#include <unordered_map>
#include <cmath>


struct SpotStats {
    float bias = 0.f;       // [-2, +2], applied as round(bias) on Delay
    float trust = 1.f;      // [0, 1]
    int pessimism = 0;
    int n = 0;
    int saved = 0;
    int diedAfterAssist = 0;
};

struct ModeLearn {
    float capMul = 1.f;     // [0.5, 2.0]
    std::unordered_map<int, SpotStats> spots; // key = x-bucket
};

// Per-level learned assist tuning. Window (ticks) is NEVER touched.
class LevelProfile {
public:
    static constexpr float SPOT_SIZE = 90.f; // ~3 blocks
    static constexpr float TRUST_PASS_THRESHOLD = 0.35f;
    static constexpr int TRUST_PASS_MIN_N = 4;

    static LevelProfile& get();

    void loadForLevel(GJGameLevel* level);
    void save();
    void resetCurrent();
    void clear();

    bool loaded() const { return m_loaded; }
    std::string const& key() const { return m_key; }
    int spotCount() const;

    static int spotBucket(float x);
    static std::string makeKey(GJGameLevel* level);

    SpotStats& spot(GameModeKind kind, float x);
    SpotStats const* trySpot(GameModeKind kind, float x) const;
    float capMul(GameModeKind kind) const;
    void setCapMul(GameModeKind kind, float mul);

    // Learning updates
    void noteBias(GameModeKind kind, float x, float delta, char const* reason);
    void noteTrust(GameModeKind kind, float x, float factor, char const* reason);
    void noteTrustUp(GameModeKind kind, float x, float amount, char const* reason);
    void notePessimism(GameModeKind kind, float x);
    void noteSaved(GameModeKind kind, float x);
    void noteDiedAfterAssist(GameModeKind kind, float x);

    void maybeAutosave(); // every 5 deaths
    void markDirty() { m_dirty = true; }

    // Overlay helpers
    SpotStats overlaySpot(GameModeKind kind, float x) const;

private:
    LevelProfile() = default;

    ModeLearn& mode(GameModeKind kind);
    ModeLearn const* tryMode(GameModeKind kind) const;
    void recomputeCapMul(GameModeKind kind);
    geode::Result<> writeFile() const;
    geode::Result<> readFile();

    std::string m_key;
    bool m_loaded = false;
    bool m_dirty = false;
    int m_deathsSinceSave = 0;
    ModeLearn m_modes[StageConfig::MODE_COUNT];
};

#endif // FRIRECRACKER_CORE_LEVELPROFILE_HPP
