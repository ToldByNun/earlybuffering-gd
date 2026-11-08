#ifndef BAKE_WINDOW_INDEX_HPP
#define BAKE_WINDOW_INDEX_HPP

#include "../../../config/stage_config.hpp"
#include "../../replay/replay_import.hpp"
#include <vector>

#include "../bake_map.hpp"

class BakeWindowIndex {
public:
    static int refineSelfWindows(BakeMap& owner, PlayLayer* layer, int startSnapshotSlot,
                                 std::vector<BakedClick>& clicks, int bakeRadius, int budgetSims,
                                 std::atomic<bool>* cancel);
    static std::vector<BakedClick const*> nextClicksAfter(BakeMap const& owner, float playerX,
                                                          bool down, bool player2,
                                                          std::optional<GameModeKind> preferMode,
                                                          int maxCount);
    static int mergeSelfClicks(BakeMap& owner, std::vector<BakedClick> const& incoming);
    static float windowHalfW(int earliest, int latest);
    static BakedClick const* findNear(BakeMap const& owner, float playerX, int approxFrame,
                                      bool down, bool player2,
                                      std::optional<GameModeKind> preferMode);
    static bool testShiftedClick(BakeMap const& owner, PlayLayer* layer, int startSnapshotSlot,
                                 std::vector<ReplayEvent> timeline, int eventIndex, int testFrame,
                                 int untilFrame, float deltaTime, int substeps);

private:
    BakeWindowIndex() = delete;
};

#endif
