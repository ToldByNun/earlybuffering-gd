#ifndef BAKE_REPLAY_GENERATOR_HPP
#define BAKE_REPLAY_GENERATOR_HPP

#include "../bake_map.hpp"

class BakeReplayGenerator {
public:
    static geode::Result<int> bakeReplay(BakeMap& owner, PlayLayer* layer, int startSnapshotSlot,
                                         int bakeRadius);

private:
    BakeReplayGenerator() = delete;
};

#endif
