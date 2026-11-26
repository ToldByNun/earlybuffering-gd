#ifndef SIMULATION_WINDOW_HPP
#define SIMULATION_WINDOW_HPP

// X-only capture window: every Y remains eligible, including incoming movers.
class EbSimulationWindow {
public:
    static constexpr float kMinimumReach = 15.f * 30.f;

    EbSimulationWindow() = default;
    EbSimulationWindow(float playerX, float ahead, float behind);
    bool overlaps(float left, float width) const;
    bool contains(float left, float width) const;

private:
    float m_minX = 0.f;
    float m_maxX = 0.f;
    bool m_valid = false;
};

#endif
