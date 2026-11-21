#ifndef NEURAL_VIZ_SNAPSHOT_HPP
#define NEURAL_VIZ_SNAPSHOT_HPP

#include "neural_model.hpp"
#include <cstdint>

// Last-seen AI perception + ranking for the in-level Visualize AI overlay.
// Exact hitboxes are GD getObjectRect/OBB copies; cells are the quantized grid.
class EbNeuralVizHitbox {
public:
    std::uint8_t channelBits = 0;
    bool oriented = false;
    float aabbX = 0.0f;
    float aabbY = 0.0f;
    float aabbW = 0.0f;
    float aabbH = 0.0f;
    float cornerX[4]{};
    float cornerY[4]{};

private:
};

class EbNeuralVizSnapshot {
public:
    static constexpr int kMaxRanked = 8;
    static constexpr int kMaxHitboxes = 128;

    bool valid = false;
    float originX = 0.0f;
    float originY = 0.0f;
    float playerX = 0.0f;
    float playerY = 0.0f;
    float playerSpeed = 1.0f;
    bool goingLeft = false;
    bool sideways = false;
    bool training = false;
    bool hasWeights = false;
    int rankedCount = 0;
    int rankedDelays[kMaxRanked]{};
    float rankedConfidence[kMaxRanked]{};
    int hintDelay = -1;
    int lastArmedDelay = -1;
    float lastArmedConfidence = 0.0f;
    int updates = 0;
    int inferences = 0;
    int verifies = 0;
    int verifyHits = 0;
    int hitboxCount = 0;
    EbNeuralVizHitbox hitboxes[kMaxHitboxes]{};
    // Player ground-truth rect (OpenHack accurate-player path).
    bool playerHitboxValid = false;
    float playerAabbX = 0.0f;
    float playerAabbY = 0.0f;
    float playerAabbW = 0.0f;
    float playerAabbH = 0.0f;
    std::uint8_t cells[EbNeuralModel::kGridCells]{};

private:
};

#endif
