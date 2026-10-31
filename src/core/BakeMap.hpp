#ifndef EARLYBUFFER_CORE_BAKEMAP_HPP
#define EARLYBUFFER_CORE_BAKEMAP_HPP

#include "ReplayImport.hpp"
#include "../config/StageConfig.hpp"

#include <Geode/Geode.hpp>
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>


struct BakedClick {
    int nominalFrame = 0;
    int earliest = 0;
    int latest = 0;
    bool down = true;
    bool player2 = false;
    float x = 0.f;
    GameModeKind mode = GameModeKind::Cube;
};

struct BakeMapData {
    std::string levelKey;
    std::string sourceReplay;
    double tps = 240.0;
    std::string method = "replay"; // "replay" | "brute" | "self"
    std::vector<BakedClick> clicks;
};

struct BakeProgress {
    std::atomic<int> done{0};
    std::atomic<int> total{0};
    std::atomic<bool> running{false};
    std::atomic<bool> cancel{false};

    std::mutex statusMutex;
    std::string status;

    void setStatus(std::string s) {
        std::lock_guard lock(statusMutex);
        status = std::move(s);
    }
    std::string getStatus() {
        std::lock_guard lock(statusMutex);
        return status;
    }
    float fraction() const {
        int t = total.load();
        if (t <= 0) return 0.f;
        return static_cast<float>(done.load()) / static_cast<float>(t);
    }
};

enum class BakeMethod {
    Replay,     // binary-search windows around imported replay clicks
    BruteForce  // no replay: death-driven exhaustive click search
};

class BakeMap {
public:
    static constexpr float LOOKUP_X_TOL = 120.f;

    static BakeMap& get();

    void clear();
    void loadForLevel(GJGameLevel* level);
    void save() const;

    bool loaded() const { return m_loaded; }
    BakeMapData const& data() const { return m_data; }
    ImportedReplay const* pendingReplay() const {
        return m_hasReplay ? &m_replay : nullptr;
    }

    void setPendingReplay(ImportedReplay replay);
    void clearPendingReplay();

    BakeProgress& progress() { return m_progress; }
    BakeProgress const& progress() const { return m_progress; }
    bool isBaking() const { return m_progress.running.load(); }

    // Offline bake using the current PlayLayer + start snapshot.
    // Replay = binary search; BruteForce = exhaustive + thread pool.
    geode::Result<int> bake(
        PlayLayer* layer,
        int startSnapshotSlot,
        int bakeRadius,
        BakeMethod method = BakeMethod::Replay
    );

    // Prefer clicks whose mode matches preferMode within LOOKUP_X_TOL.
    // If none match, fall back to any mode (legacy maps / missing mode).
    // Pass preferMode=nullopt to skip the mode preference pass entirely.
    BakedClick const* findNear(
        float playerX, int approxFrame, bool down, bool player2,
        std::optional<GameModeKind> preferMode = std::nullopt
    ) const;

    // Next bake/self-bake clicks after playerX (same down/player2; prefer mode).
    // Sorted by ascending x. Used by smart bake-chain (planSequence).
    std::vector<BakedClick const*> nextClicksAfter(
        float playerX, bool down, bool player2,
        std::optional<GameModeKind> preferMode,
        int maxCount
    ) const;

    // Merge self-bake point-windows: dedup by down/player2/near x+frame → union
    // earliest/latest. Sets method="self" when the map was empty or already self.
    // Returns number of clicks added or widened. Cap kMaxMapClicks.
    static constexpr float MERGE_X_TOL = 40.f;
    static constexpr int MERGE_FRAME_TOL = 30;
    static constexpr size_t kMaxMapClicks = 512;
    int mergeSelfClicks(std::vector<BakedClick> const& incoming);

    // Budgeted binary-search earliest/latest around each click (replay-style),
    // using a synthetic timeline from `clicks`. Stops when budgetSims or cancel.
    // Returns number of clicks whose window was expanded. In-place update.
    int refineSelfWindows(
        PlayLayer* layer,
        int startSnapshotSlot,
        std::vector<BakedClick>& clicks,
        int bakeRadius,
        int budgetSims,
        std::atomic<bool>* cancel = nullptr
    );

    std::string statusLine() const;
    std::string pauseStatusLine() const;

private:
    BakeMap() = default;

    geode::Result<> writeFile() const;
    geode::Result<> readFile();

    bool testShiftedClick(
        PlayLayer* layer,
        int startSnapshotSlot,
        std::vector<ReplayEvent> timeline,
        int eventIndex,
        int testFrame,
        int untilFrame,
        float dt,
        int substeps = 1
    ) const;

    geode::Result<int> bakeReplay(
        PlayLayer* layer, int startSnapshotSlot, int bakeRadius
    );
    geode::Result<int> bakeBrute(
        PlayLayer* layer, int startSnapshotSlot, int bakeRadius
    );

    BakeMapData m_data;
    ImportedReplay m_replay;
    bool m_hasReplay = false;
    bool m_loaded = false;
    // Set by mergeSelfClicks; cleared by save(). Avoids disk I/O on death flush.
    mutable bool m_dirty = false;
    BakeProgress m_progress;
    mutable std::mutex m_simMutex;
};

#endif // EARLYBUFFER_CORE_BAKEMAP_HPP
