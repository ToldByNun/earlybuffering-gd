#include "neural_grid.hpp"
#include <algorithm>
#include <cmath>

std::uint8_t EbNeuralGrid::channelBitsForConfirmed(int objectKind) {
    switch (objectKind) {
    case 1:
        return kChannelSolid;
    case 2:
        return kChannelHazard;
    case 3:
        return kChannelOrbPad;
    default:
        return 0;
    }
}

float EbNeuralGrid::gridStartX(float playerX) {
    return std::floor((playerX - kCellSize) / kCellSize) * kCellSize;
}

float EbNeuralGrid::gridStartY(float playerY) {
    // Center the player vertically in the 240u window (Nova-style local frame).
    return std::floor((playerY - kGridHeight * 0.5f) / kCellSize) * kCellSize;
}

void EbNeuralGrid::clear() {
    for (int index = 0; index < EbNeuralModel::kGridCells; ++index)
        m_cells[index] = 0;
}

void EbNeuralGrid::stampObject(float gridOriginX, float gridOriginY,
                               EbNeuralGridObject const& object) {
    if (object.channelBits == 0 || object.width <= 0.0f || object.height <= 0.0f)
        return;
    int const minX = std::max(
        0, static_cast<int>(std::floor((object.x - gridOriginX) / kCellSize)));
    int const maxX = std::min(
        EbNeuralModel::kGridCols - 1,
        static_cast<int>(
            std::floor((object.x + object.width - gridOriginX - 0.01f) / kCellSize)));
    int const minY = std::max(
        0, static_cast<int>(std::floor((object.y - gridOriginY) / kCellSize)));
    int const maxY = std::min(
        EbNeuralModel::kGridRows - 1,
        static_cast<int>(
            std::floor((object.y + object.height - gridOriginY - 0.01f) / kCellSize)));
    if (minX > maxX || minY > maxY)
        return;
    for (int row = minY; row <= maxY; ++row) {
        for (int col = minX; col <= maxX; ++col) {
            int const cellIndex = row * EbNeuralModel::kGridCols + col;
            m_cells[cellIndex] =
                static_cast<std::uint8_t>(m_cells[cellIndex] | object.channelBits);
        }
    }
}

void EbNeuralGrid::rasterize(float playerX, float playerY, EbNeuralGridObject const* objects,
                             int objectCount) {
    clear();
    if (!objects || objectCount <= 0)
        return;
    float const originX = gridStartX(playerX);
    float const originY = gridStartY(playerY);
    for (int index = 0; index < objectCount; ++index)
        stampObject(originX, originY, objects[index]);
}

void EbNeuralGrid::scatterTo(float* features) const {
    if (!features)
        return;
    for (int index = 0; index < EbNeuralModel::kGridDim; ++index)
        features[index] = 0.0f;
    for (int cell = 0; cell < EbNeuralModel::kGridCells; ++cell) {
        std::uint8_t const bits = m_cells[cell];
        if (!bits)
            continue;
        for (int channel = 0; channel < 6; ++channel) {
            if ((bits & (1u << channel)) == 0)
                continue;
            int const featureIndex = channel * EbNeuralModel::kGridCells + cell;
            features[featureIndex] = 1.0f;
        }
    }
}

void EbNeuralGrid::collectActiveIndices(std::vector<int>& outIndices) const {
    outIndices.clear();
    for (int cell = 0; cell < EbNeuralModel::kGridCells; ++cell) {
        std::uint8_t const bits = m_cells[cell];
        if (!bits)
            continue;
        for (int channel = 0; channel < 6; ++channel) {
            if ((bits & (1u << channel)) == 0)
                continue;
            outIndices.push_back(channel * EbNeuralModel::kGridCells + cell);
        }
    }
}

int EbNeuralGrid::activeCellCount() const {
    int count = 0;
    for (int cell = 0; cell < EbNeuralModel::kGridCells; ++cell) {
        if (m_cells[cell])
            ++count;
    }
    return count;
}

bool EbNeuralGrid::selfCheck(std::string& report) {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };
    expect(EbNeuralModel::kGridDim == 3456, "grid-dim");
    expect(channelBitsForConfirmed(0) == 0, "unknown-zero");
    expect(channelBitsForConfirmed(1) == kChannelSolid, "solid-bit");
    expect(channelBitsForConfirmed(99) == 0, "invented-kind-zero");

    EbNeuralGrid grid;
    EbNeuralGridObject block;
    block.x = 100.0f;
    block.y = 100.0f;
    block.width = 30.0f;
    block.height = 30.0f;
    block.channelBits = kChannelSolid;
    grid.rasterize(100.0f, 100.0f, &block, 1);
    expect(grid.activeCellCount() > 0, "stamp-active");

    EbNeuralGridObject unknown = block;
    unknown.channelBits = channelBitsForConfirmed(0);
    EbNeuralGrid empty;
    empty.rasterize(100.0f, 100.0f, &unknown, 1);
    expect(empty.activeCellCount() == 0, "unknown-no-stamp");

    std::vector<float> features(static_cast<size_t>(EbNeuralModel::kFeatureDim), 0.0f);
    grid.scatterTo(features.data());
    std::vector<int> active;
    grid.collectActiveIndices(active);
    int lit = 0;
    for (int index = 0; index < EbNeuralModel::kGridDim; ++index) {
        if (features[static_cast<size_t>(index)] != 0.0f)
            ++lit;
    }
    expect(lit > 0, "scatter-lit");
    expect(lit == static_cast<int>(active.size()), "scatter-matches-indices");

    if (fails == 0) {
        report = "NeuralGrid OK";
        return true;
    }
    report = std::string("NeuralGrid fails: ") + report;
    return false;
}
