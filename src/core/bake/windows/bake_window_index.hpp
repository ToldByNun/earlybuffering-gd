#ifndef BAKE_WINDOW_INDEX_HPP
#define BAKE_WINDOW_INDEX_HPP

#include "../../../config/stage_config.hpp"
#include "../../replay/replay_import.hpp"
#include <vector>

#include "../bake_map.hpp"

class EbBakeWindowIndex {
public:
    static int refineSelfWindows(EbBakeMap& owner, PlayLayer* layer, int startSnapshotSlot,
                                 std::vector<EbBakedClick>& clicks, int bakeRadius, int budgetSims,
                                 std::atomic<bool>* cancel);
    static std::vector<EbBakedClick const*> nextClicksAfter(EbBakeMap const& owner, float playerX,
                                                          bool down, bool player2,
                                                          std::optional<GameModeKind> preferMode,
                                                          int maxCount);
    static int mergeSelfClicks(EbBakeMap& owner, std::vector<EbBakedClick> const& incoming);
    static float windowHalfW(int earliest, int latest);
    static EbBakedClick const* findNear(EbBakeMap const& owner, float playerX, int approxFrame,
                                      bool down, bool player2,
                                      std::optional<GameModeKind> preferMode);
    static bool testShiftedClick(EbBakeMap const& owner, PlayLayer* layer, int startSnapshotSlot,
                                 std::vector<EbReplayEvent> timeline, int eventIndex, int testFrame,
                                 int untilFrame, float deltaTime, int substeps);

private:
    EbBakeWindowIndex() = delete;
};

#endif
