#ifndef BAKE_REPLAY_GENERATOR_HPP
#define BAKE_REPLAY_GENERATOR_HPP

#include "../bake_map.hpp"

class EbBakeReplayGenerator {
public:
    static geode::Result<int> bakeReplay(EbBakeMap& owner, PlayLayer* layer, int startSnapshotSlot,
                                         int bakeRadius);

private:
    EbBakeReplayGenerator() = delete;
};

#endif
