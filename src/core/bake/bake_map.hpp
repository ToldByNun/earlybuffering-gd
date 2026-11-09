#ifndef BAKE_MAP_HPP
#define BAKE_MAP_HPP

class EbReplayJumpJob;

#include "../../config/stage_config.hpp"
#include "../replay/replay_import.hpp"
#include <Geode/Geode.hpp>
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class EbBakedClick {
public:
    int nominalFrame = 0;
    int earliest = 0;
    int latest = 0;
    bool down = true;
    bool player2 = false;
    float x = 0.f;
    GameModeKind mode = GameModeKind::Cube;
    // Release after dash/hold-orb (or tagged hold). Viz/gate: purple on discrete modes.
    bool holdAssist = false;

private:
};

class EbBakeMapData {
public:
    std::string levelKey;
    std::string sourceReplay;
    double tps = 240.0;
    std::string method = "replay"; // "replay" | "brute" | "self"
    std::vector<EbBakedClick> clicks;

private:
};

class EbBakeProgress {
public:
    std::atomic<int> done{0};
    std::atomic<int> total{0};
    std::atomic<bool> running{false};
    std::atomic<bool> cancel{false};

    std::mutex statusMutex;
    std::string status;

    void setStatus(std::string text) {
        std::lock_guard lock(statusMutex);
        status = std::move(text);
    }
    std::string getStatus() {
        std::lock_guard lock(statusMutex);
        return status;
    }
    float fraction() const {
        int tick = total.load();
        if (!(tick <= 0)) {
            return static_cast<float>(done.load()) / static_cast<float>(tick);
        }
        return 0.f;
    }

private:
};

enum class BakeMethod {
    Replay,    // binary-search windows around imported replay clicks
    BruteForce // no replay: death-driven exhaustive click search
};

class EbBakeMap {
public:
    static constexpr float LOOKUP_X_TOL = 120.f;

    // Drawn bake-window half-width (matches BakePathVisualizer green/purple boxes).
    static float windowHalfW(int earliest, int latest);

    static EbBakeMap& get();

    void clear();
    void loadForLevel(GJGameLevel* level);
    void save() const;

    bool loaded() const {
        return m_loaded;
    }
    EbBakeMapData const& data() const {
        return m_data;
    }
    EbImportedReplay const* pendingReplay() const {
        return m_hasReplay ? &m_replay : nullptr;
    }

    void setPendingReplay(EbImportedReplay replay);
    void clearPendingReplay();

    EbBakeProgress& progress() {
        return m_progress;
    }
    EbBakeProgress const& progress() const {
        return m_progress;
    }
    bool isBaking() const {
        return m_progress.running.load();
    }

    // Offline bake using the current PlayLayer + start snapshot.
    // Replay = binary search; BruteForce = exhaustive + thread pool.
    geode::Result<int> bake(PlayLayer* layer, int startSnapshotSlot, int bakeRadius,
                            BakeMethod method = BakeMethod::Replay);

    // Prefer clicks whose mode matches preferMode within LOOKUP_X_TOL.
    // If none match, fall back to any mode (legacy maps / missing mode).
    // Pass preferMode=nullopt to skip the mode preference pass entirely.
    EbBakedClick const* findNear(float playerX, int approxFrame, bool down, bool player2,
                               std::optional<GameModeKind> preferMode = std::nullopt) const;

    // Next bake/self-bake clicks after playerX (same down/player2; prefer mode).
    // Sorted by ascending x. Used by smart bake-chain (planSequence).
    std::vector<EbBakedClick const*> nextClicksAfter(float playerX, bool down, bool player2,
                                                   std::optional<GameModeKind> preferMode,
                                                   int maxCount) const;

    // Merge self-bake point-windows: dedup by down/player2/near x+frame → union
    // earliest/latest. Sets method="self" when the map was empty or already self.
    // Returns number of clicks added or widened. Cap kMaxMapClicks.
    static constexpr float MERGE_X_TOL = 40.f;
    static constexpr int MERGE_FRAME_TOL = 30;
    static constexpr size_t kMaxMapClicks = 512;
    int mergeSelfClicks(std::vector<EbBakedClick> const& incoming);

    // Budgeted binary-search earliest/latest around each click (replay-style),
    // using a synthetic timeline from `clicks`. Stops when budgetSims or cancel.
    // Returns number of clicks whose window was expanded. In-place update.
    int refineSelfWindows(PlayLayer* layer, int startSnapshotSlot, std::vector<EbBakedClick>& clicks,
                          int bakeRadius, int budgetSims, std::atomic<bool>* cancel = nullptr);

    std::string statusLine() const;
    std::string pauseStatusLine() const;

private:
    friend class EbServiceStore;

    friend class EbBakeWindowIndex;

    friend class EbBakeBruteGenerator;

    friend class EbBakeReplayGenerator;

    EbBakeMap() = default;

    geode::Result<> writeFile() const;
    geode::Result<> readFile();

    bool testShiftedClick(PlayLayer* layer, int startSnapshotSlot,
                          std::vector<EbReplayEvent> timeline, int eventIndex, int testFrame,
                          int untilFrame, float deltaTime, int substeps = 1) const;

    geode::Result<int> bakeReplay(PlayLayer* layer, int startSnapshotSlot, int bakeRadius);
    geode::Result<int> bakeBrute(PlayLayer* layer, int startSnapshotSlot, int bakeRadius);

    EbBakeMapData m_data;
    EbImportedReplay m_replay;
    bool m_hasReplay = false;
    bool m_loaded = false;
    // Set by mergeSelfClicks; cleared by save(). Avoids disk I/O on death flush.
    mutable bool m_dirty = false;
    EbBakeProgress m_progress;
    mutable std::mutex m_simMutex;
};

#include <Geode/utils/file.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

class EbBakeMapImplementation {
public:
    static std::filesystem::path bakesDir();
    static std::filesystem::path bakePath(std::string const& key);
    static int nextJumpFrame(std::vector<EbReplayEvent> const& ev, int fromIndex, bool player2);
    static int resolveRadius(int bakeRadius, BakeMethod method);
    static void ensureMinHoldTicks(std::vector<EbReplayEvent>& ev, int minHold = 1);
    static double resolveBakeTps(double srcTps, double liveTps);
    static int scaleBakeTicks(int ticks, double bakeTps);
    static std::vector<EbReplayEvent> prepareBakeTimeline(std::vector<EbReplayEvent> const& src,
                                                        double srcTps, double bakeTps,
                                                        bool forceSoloP1);
    static unsigned workerCount();

private:
    EbBakeMapImplementation() = delete;
};

class EbReplayJumpJob {
public:
    int eventIndex = 0;
    int frame = 0;
    bool down = true;
    bool player2 = false;
    int until = 0;

private:
};

#endif
