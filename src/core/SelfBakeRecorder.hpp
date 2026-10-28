#ifndef EARLYBUFFER_CORE_SELFBAKERECORDER_HPP
#define EARLYBUFFER_CORE_SELFBAKERECORDER_HPP

#include "../config/StageConfig.hpp"

#include <atomic>
#include <string>
#include <vector>

class PlayLayer;
class PlayerObject;

// Record non-injected (vanilla) jump inputs and merge into BakeMap as method
// "self". Optional budgeted refine expands earliest/latest from start snapshot.
class SelfBakeRecorder {
public:
    static constexpr size_t kMaxPending = 256;
    static constexpr size_t kMaxMapClicks = 512;
    static constexpr float kDedupXTol = 40.f;
    static constexpr int kDedupFrameTol = 30;
    // Max runBakeTimeline probes per flush (binary search is log-radius each).
    static constexpr int kRefineBudgetSims = 64;

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

    // Merge pending samples into BakeMap. allowRefine uses start snapshot when safe.
    void flushSegment(PlayLayer* layer, char const* reason, bool allowRefine = true);

    void clearPending();
    void cancelRefine();
    size_t pendingCount() const { return m_pending.size(); }

private:
    SelfBakeRecorder() = default;

    bool shouldRecord(PlayLayer* layer, PlayerObject* player) const;

    std::vector<Sample> m_pending;
    std::string m_levelKey;
    std::atomic<bool> m_cancelRefine{false};
};

#endif // EARLYBUFFER_CORE_SELFBAKERECORDER_HPP
