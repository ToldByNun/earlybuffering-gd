#pragma once

#include "ReplayImport.hpp"
#include "../config/StageConfig.hpp"

#include <Geode/Geode.hpp>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <optional>

namespace frire {

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
    std::string method = "replay"; // "replay" | "brute"
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

    BakedClick const* findNear(
        float playerX, int approxFrame, bool down, bool player2
    ) const;

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
        float dt
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
    BakeProgress m_progress;
    mutable std::mutex m_simMutex;
};

}
