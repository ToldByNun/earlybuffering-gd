#include "live_horizon_controller.hpp"
#include <algorithm>
#include <cmath>

float EbLiveHorizonController::scale(int mode, bool player1, bool down) const {
    if (mode < 0 || mode >= 8)
        return 1.f;
    float const value = m_scales[mode][player1 ? 0 : 1][down ? 0 : 1];
    return value > 0.f ? value : 1.f;
}

void EbLiveHorizonController::observe(int mode, bool player1, bool down, bool budgetHit,
                                      int spentUs, int budgetUs) {
    if (mode < 0 || mode >= 8 || budgetUs <= 0 || spentUs < 0)
        return;
    float value = scale(mode, player1, down);
    if (budgetHit)
        value = std::max(0.5f, value * 0.85f);
    else if (spentUs > 0 && spentUs <= budgetUs / 2)
        value = std::min(1.f, value * 1.03f);
    m_scales[mode][player1 ? 0 : 1][down ? 0 : 1] = value;
}

void EbLiveHorizonController::reset() {
    for (auto& mode : m_scales) {
        for (auto& player : mode) {
            for (float& value : player)
                value = 0.f;
        }
    }
}

int EbLiveHorizonController::scaledCap(int nominalCap, float value) {
    if (nominalCap <= 0)
        return 1;
    if (!std::isfinite(value))
        value = 1.f;
    return std::clamp(static_cast<int>(std::ceil(nominalCap * std::clamp(value, 0.5f, 1.f))), 1,
                      nominalCap);
}
