#ifndef EARLYBUFFER_CORE_SELFBAKERECORDER_HPP
#define EARLYBUFFER_CORE_SELFBAKERECORDER_HPP

#include "../config/StageConfig.hpp"

#include <string>
#include <vector>

class PlayLayer;
class PlayerObject;

// Skeleton: record non-injected (vanilla) jump inputs and merge into BakeMap
// as method "self" point-windows. Refine (binary-search windows) is Commit 5.
class SelfBakeRecorder {
public:
    static constexpr size_t kMaxPending = 256;
    static constexpr size_t kMaxMapClicks = 512;
    static constexpr float kDedupXTol = 40.f;
    static constexpr int kDedupFrameTol = 30;

    struct Sample {
        int frame = 0;
        float x = 0.f;
        GameModeKind mode = GameModeKind::Cube;
        bool down = true;
        bool player2 = false;
    };

    static SelfBakeRecorder& get();

    void onLevelInit(PlayLayer* layer);
    void onLevelQuit();

    // Call only for real user clicks that pass through (not assist inject / swallow).
    void noteVanillaInput(
        PlayLayer* layer, PlayerObject* player, bool player1,
        bool down, int attemptTick, float simDt
    );

    // Merge pending samples into BakeMap (method=self) and clear the segment.
    void flushSegment(PlayLayer* layer, char const* reason);

    void clearPending();
    size_t pendingCount() const { return m_pending.size(); }

private:
    SelfBakeRecorder() = default;

    bool shouldRecord(PlayLayer* layer, PlayerObject* player) const;

    std::vector<Sample> m_pending;
    std::string m_levelKey;
};

#endif // EARLYBUFFER_CORE_SELFBAKERECORDER_HPP
