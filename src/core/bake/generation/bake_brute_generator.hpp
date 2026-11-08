#ifndef BAKE_BRUTE_GENERATOR_HPP
#define BAKE_BRUTE_GENERATOR_HPP

#include "../bake_map.hpp"

class BakeBruteGenerator {
public:
    static geode::Result<int> bakeBrute(BakeMap& owner, PlayLayer* layer, int startSnapshotSlot,
                                        int bakeRadius);

private:
    BakeBruteGenerator() = delete;
};

#endif
