#ifndef BAKE_BRUTE_GENERATOR_HPP
#define BAKE_BRUTE_GENERATOR_HPP

#include "../bake_map.hpp"

class EbBakeBruteGenerator {
public:
    static geode::Result<int> bakeBrute(EbBakeMap& owner, PlayLayer* layer, int startSnapshotSlot,
                                        int bakeRadius);

private:
    EbBakeBruteGenerator() = delete;
};

#endif
