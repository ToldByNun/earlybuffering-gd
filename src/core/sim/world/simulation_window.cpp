#include "simulation_window.hpp"
#include <algorithm>
#include <cmath>

EbSimulationWindow::EbSimulationWindow(float playerX, float ahead, float behind) {
    if (!std::isfinite(playerX) || !std::isfinite(ahead) || !std::isfinite(behind))
        return;
    m_minX = playerX - std::max(kMinimumReach, behind);
    m_maxX = playerX + std::max(kMinimumReach, ahead);
    m_valid = std::isfinite(m_minX) && std::isfinite(m_maxX);
}

bool EbSimulationWindow::overlaps(float left, float width) const {
    // Invalid bounds cannot establish that an object is safely outside.
    if (!m_valid || !std::isfinite(left) || !std::isfinite(width) || width < 0.f)
        return true;
    return left <= m_maxX && left + width >= m_minX;
}

bool EbSimulationWindow::contains(float left, float width) const {
    if (!m_valid || !std::isfinite(left) || !std::isfinite(width) || width < 0.f)
        return false;
    return left >= m_minX && left + width <= m_maxX;
}
