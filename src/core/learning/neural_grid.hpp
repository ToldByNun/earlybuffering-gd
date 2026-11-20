#ifndef NEURAL_GRID_HPP
#define NEURAL_GRID_HPP

#include "neural_model.hpp"
#include <cstdint>
#include <string>
#include <vector>

// Nova-like player-relative raster. Only confirmed category channels are written;
// unknown object kinds leave all channels zero (no invented Nova labels).
// Geometry is GameObject hitbox space (getObjectRect / oriented corners), not sprite
// contentSize — contentSize inflated the perception window past real collision.
class EbNeuralGridObject {
public:
    // Axis-aligned hitbox (getObjectRect). Always set for culling / AABB stamp.
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    // When true, stamp cells whose centers lie inside the oriented quad (OBB2D).
    bool oriented = false;
    float cornerX[4]{};
    float cornerY[4]{};
    // Bits 0..5 map to raster channels 0..5. Channels 6..8 reserved until confirmed.
    std::uint8_t channelBits = 0;

private:
};

class EbNeuralGrid {
public:
    static constexpr float kCellSize = 15.0f;
    static constexpr float kGridWidth = kCellSize * EbNeuralModel::kGridCols;
    static constexpr float kGridHeight = kCellSize * EbNeuralModel::kGridRows;

    // Confirmed channel bits (Nova channels 0..5). Do not invent 6..8 yet.
    static constexpr std::uint8_t kChannelSolid = 1u << 0;
    static constexpr std::uint8_t kChannelHazard = 1u << 1;
    static constexpr std::uint8_t kChannelOrbPad = 1u << 2;

    // objectKind: 0 unknown, 1 solid, 2 hazard, 3 orb/pad. Unknown → 0 bits.
    static std::uint8_t channelBitsForConfirmed(int objectKind);

    static float gridStartX(float playerX);
    static float gridStartY(float playerY);

    void clear();
    void rasterize(float playerX, float playerY, EbNeuralGridObject const* objects, int objectCount);
    void scatterTo(float* features) const;
    void collectActiveIndices(std::vector<int>& outIndices) const;
    int activeCellCount() const;
    // Copy packed channel bits per cell (same layout as internal stamp buffer).
    void copyCells(std::uint8_t* outCells) const;

    static bool selfCheck(std::string& report);

private:
    std::uint8_t m_cells[EbNeuralModel::kGridCells]{};

    void stampObject(float gridOriginX, float gridOriginY, EbNeuralGridObject const& object);
};

#endif
