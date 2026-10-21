#pragma once

#include "ReplayImport.hpp"
#include "../config/StageConfig.hpp"

#include <Geode/Geode.hpp>
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
    std::vector<BakedClick> clicks;
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

    // Offline bake using the current PlayLayer + start snapshot.
    // Returns number of baked clicks, or Err.
    geode::Result<int> bake(PlayLayer* layer, int startSnapshotSlot, int bakeRadius);

    // Runtime lookup near player X / estimated frame.
    BakedClick const* findNear(
        float playerX, int approxFrame, bool down, bool player2
    ) const;

    std::string statusLine() const;

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

    BakeMapData m_data;
    ImportedReplay m_replay;
    bool m_hasReplay = false;
    bool m_loaded = false;
};

}
