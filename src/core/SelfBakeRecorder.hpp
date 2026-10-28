#ifndef EARLYBUFFER_CORE_SELFBAKERECORDER_HPP
#define EARLYBUFFER_CORE_SELFBAKERECORDER_HPP

#include "../config/StageConfig.hpp"
#include "BakeMap.hpp"

#include <atomic>
#include <string>
#include <vector>


class PlayLayer;
class PlayerObject;

// Record non-injected (vanilla) jump inputs into an in-memory buffer.
// Hot paths (death / reset / checkpoint) NEVER wait on bake work.
//
// Drain model:
//   noteVanillaInput → m_pending (cheap push)
//   onPhysicsTick    → merge a small batch into BakeMap + optional budgeted refine
//   quit / complete  → force-merge remaining (no refine) so nothing is lost
//
// Refine uses GD physics and must stay on the main thread — we amortize it
// across ticks (kIdleRefineSimsPerTick) instead of blocking one frame.
class SelfBakeRecorder {
public:
    static constexpr size_t kMaxPending = 512;
    static constexpr size_t kIdleMergePerTick = 12;
    static constexpr int kIdleRefineSimsPerTick = 4;
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

    // Amortized main-thread drain — call once per physics tick while alive.
    void onPhysicsTick(PlayLayer* layer);

    // Force-merge pending into BakeMap (no refine). Use on quit / complete only.
    void flushMergeOnly(PlayLayer* layer, char const* reason);

    // Legacy name kept for call sites that meant "finish segment now".
    // Hot-path callers should NOT use this — prefer leaving work to onPhysicsTick.
    void flushSegment(PlayLayer* layer, char const* reason, bool allowRefine = true);

    void clearPending();
    void cancelRefine();
    size_t pendingCount() const { return m_pending.size(); }
    size_t refineQueueCount() const { return m_refineQueue.size(); }

private:
    SelfBakeRecorder() = default;

    bool shouldRecord(PlayLayer* layer, PlayerObject* player) const;
    bool canDrain(PlayLayer* layer) const;
    void mergeSamples(std::vector<Sample>& samples, char const* reason);
    void drainRefine(PlayLayer* layer);

    std::vector<Sample> m_pending;
    // Point windows already merged; waiting for budgeted refine expansion.
    std::vector<BakedClick> m_refineQueue;
    std::string m_levelKey;
    std::atomic<bool> m_cancelRefine{false};
};

#endif // EARLYBUFFER_CORE_SELFBAKERECORDER_HPP
