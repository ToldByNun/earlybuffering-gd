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

    // Center-sample: a cell lights iff its center lies inside the GD hitbox.
    // Guarantees neural perception ⊆ real collision geometry (no fat contentSize).
    auto const stampAabb = [&](float minWorldX, float minWorldY, float maxWorldX, float maxWorldY) {
        int const minX = std::max(
            0, static_cast<int>(std::floor((minWorldX - gridOriginX) / kCellSize)));
        int const maxX = std::min(
            EbNeuralModel::kGridCols - 1,
            static_cast<int>(std::floor((maxWorldX - gridOriginX) / kCellSize)));
        int const minY = std::max(
            0, static_cast<int>(std::floor((minWorldY - gridOriginY) / kCellSize)));
        int const maxY = std::min(
            EbNeuralModel::kGridRows - 1,
            static_cast<int>(std::floor((maxWorldY - gridOriginY) / kCellSize)));
        if (minX > maxX || minY > maxY)
            return;
        for (int row = minY; row <= maxY; ++row) {
            for (int col = minX; col <= maxX; ++col) {
                float const centerX =
                    gridOriginX + (static_cast<float>(col) + 0.5f) * kCellSize;
                float const centerY =
                    gridOriginY + (static_cast<float>(row) + 0.5f) * kCellSize;
                if (centerX < minWorldX || centerX > maxWorldX || centerY < minWorldY ||
                    centerY > maxWorldY)
                    continue;
                int const cellIndex = row * EbNeuralModel::kGridCols + col;
                m_cells[cellIndex] =
                    static_cast<std::uint8_t>(m_cells[cellIndex] | object.channelBits);
            }
        }
    };

    if (object.oriented) {
        float aabbMinX = object.cornerX[0];
        float aabbMaxX = object.cornerX[0];
        float aabbMinY = object.cornerY[0];
        float aabbMaxY = object.cornerY[0];
        for (int corner = 1; corner < 4; ++corner) {
            aabbMinX = std::min(aabbMinX, object.cornerX[corner]);
            aabbMaxX = std::max(aabbMaxX, object.cornerX[corner]);
            aabbMinY = std::min(aabbMinY, object.cornerY[corner]);
            aabbMaxY = std::max(aabbMaxY, object.cornerY[corner]);
        }
        int const minX = std::max(
            0, static_cast<int>(std::floor((aabbMinX - gridOriginX) / kCellSize)));
        int const maxX = std::min(
            EbNeuralModel::kGridCols - 1,
            static_cast<int>(std::floor((aabbMaxX - gridOriginX - 0.01f) / kCellSize)));
        int const minY = std::max(
            0, static_cast<int>(std::floor((aabbMinY - gridOriginY) / kCellSize)));
        int const maxY = std::min(
            EbNeuralModel::kGridRows - 1,
            static_cast<int>(std::floor((aabbMaxY - gridOriginY - 0.01f) / kCellSize)));
        auto const pointInOriented = [&](float px, float py) {
            float prev = 0.0f;
            for (int edge = 0; edge < 4; ++edge) {
                int const next = (edge + 1) & 3;
                float const ex = object.cornerX[next] - object.cornerX[edge];
                float const ey = object.cornerY[next] - object.cornerY[edge];
                float const cx = px - object.cornerX[edge];
                float const cy = py - object.cornerY[edge];
                float const cross = ex * cy - ey * cx;
                if (edge == 0)
                    prev = cross;
                else if (cross * prev < 0.0f)
                    return false;
            }
            return true;
        };
        int stamped = 0;
        if (minX <= maxX && minY <= maxY) {
            for (int row = minY; row <= maxY; ++row) {
                for (int col = minX; col <= maxX; ++col) {
                    float const centerX =
                        gridOriginX + (static_cast<float>(col) + 0.5f) * kCellSize;
                    float const centerY =
                        gridOriginY + (static_cast<float>(row) + 0.5f) * kCellSize;
                    if (!pointInOriented(centerX, centerY))
                        continue;
                    int const cellIndex = row * EbNeuralModel::kGridCols + col;
                    m_cells[cellIndex] =
                        static_cast<std::uint8_t>(m_cells[cellIndex] | object.channelBits);
                    ++stamped;
                }
            }
        }
        if (stamped > 0)
            return;
        // Bad winding / stale OBB — keep AABB so the cell never goes blank.
    }

    stampAabb(object.x, object.y, object.x + object.width, object.y + object.height);
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

void EbNeuralGrid::copyCells(std::uint8_t* outCells) const {
    if (!outCells)
        return;
    for (int cell = 0; cell < EbNeuralModel::kGridCells; ++cell)
        outCells[cell] = m_cells[cell];
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

    // Oriented diamond should stamp fewer cells than its AABB.
    EbNeuralGrid orientedGrid;
    EbNeuralGridObject diamond;
    diamond.x = 90.0f;
    diamond.y = 90.0f;
    diamond.width = 40.0f;
    diamond.height = 40.0f;
    diamond.channelBits = kChannelHazard;
    diamond.oriented = true;
    diamond.cornerX[0] = 110.0f;
    diamond.cornerY[0] = 90.0f;
    diamond.cornerX[1] = 130.0f;
    diamond.cornerY[1] = 110.0f;
    diamond.cornerX[2] = 110.0f;
    diamond.cornerY[2] = 130.0f;
    diamond.cornerX[3] = 90.0f;
    diamond.cornerY[3] = 110.0f;
    orientedGrid.rasterize(110.0f, 110.0f, &diamond, 1);
    EbNeuralGrid aabbGrid;
    EbNeuralGridObject box = diamond;
    box.oriented = false;
    aabbGrid.rasterize(110.0f, 110.0f, &box, 1);
    expect(orientedGrid.activeCellCount() > 0, "oriented-active");
    expect(orientedGrid.activeCellCount() <= aabbGrid.activeCellCount(), "oriented-tighter");

    // Ground-truth inclusion: every cell whose center lies inside an AABB stamp
    // must be lit; no lit cell center may fall outside the expanded AABB.
    EbNeuralGrid includeGrid;
    EbNeuralGridObject slab;
    slab.x = 100.0f;
    slab.y = 100.0f;
    slab.width = 45.0f;
    slab.height = 30.0f;
    slab.channelBits = kChannelSolid;
    float const originX = gridStartX(110.0f);
    float const originY = gridStartY(110.0f);
    includeGrid.rasterize(110.0f, 110.0f, &slab, 1);
    std::uint8_t cells[EbNeuralModel::kGridCells]{};
    includeGrid.copyCells(cells);
    bool inclusionOk = true;
    bool anyLit = false;
    for (int row = 0; row < EbNeuralModel::kGridRows; ++row) {
        for (int col = 0; col < EbNeuralModel::kGridCols; ++col) {
            float const cx = originX + (static_cast<float>(col) + 0.5f) * kCellSize;
            float const cy = originY + (static_cast<float>(row) + 0.5f) * kCellSize;
            bool const inside = cx >= slab.x && cx <= slab.x + slab.width && cy >= slab.y &&
                               cy <= slab.y + slab.height;
            bool const lit = cells[row * EbNeuralModel::kGridCols + col] != 0;
            if (inside && !lit)
                inclusionOk = false;
            if (lit)
                anyLit = true;
            if (lit && (cx < slab.x - kCellSize || cx > slab.x + slab.width + kCellSize ||
                        cy < slab.y - kCellSize || cy > slab.y + slab.height + kCellSize))
                inclusionOk = false;
        }
    }
    expect(anyLit, "include-any");
    expect(inclusionOk, "include-aabb-cells");

    if (fails == 0) {
        report = "NeuralGrid OK";
        return true;
    }
    report = std::string("NeuralGrid fails: ") + report;
    return false;
}
