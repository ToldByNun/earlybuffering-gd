#ifndef AI_VISUALIZER_HPP
#define AI_VISUALIZER_HPP

#include <Geode/Geode.hpp>
#include <string>

// Draws the neural Candidate Scorer local grid + ranked delay markers.
// Sibling to TrajectoryVisualizer — perception only; never arms assist.
class EbAiVisualizer {
public:
    static EbAiVisualizer& get();

    void attach(PlayLayer* layer);
    void detach(bool layerDying = false);
    void clear();

    void onPhysicsTick(PlayLayer* layer, float deltaTime);

    void setEnabled(bool on);
    bool enabled() const {
        return m_enabled;
    }

    std::string statusHint() const;

private:
    friend class EbServiceStore;

    EbAiVisualizer() = default;

    void ensureNode(PlayLayer* layer);
    void rebuild(PlayLayer* layer);

    // ~8 physics ticks ≈ 33ms @240 — viz is diagnostic, not per-frame HUD.
    static constexpr int kRebuildEveryTicks = 8;

    cocos2d::CCDrawNode* m_node = nullptr;
    bool m_enabled = false;
    int m_ticksSinceRebuild = 0;
    int m_drawnCells = 0;
    int m_drawnCandidates = 0;
    int m_lastUpdates = -1;
};

class EbAiVisualizerImplementation {
public:
    static geode::prelude::ccColor4F solidColor(float alpha);
    static geode::prelude::ccColor4F hazardColor(float alpha);
    static geode::prelude::ccColor4F orbColor(float alpha);
    static geode::prelude::ccColor4F frameColor(float alpha);
    static geode::prelude::ccColor4F rankColor(int rank, float confidence, float alpha);
    static geode::prelude::ccColor4F armedColor(float alpha);
    static geode::prelude::ccColor4F trainPulseColor(float alpha);

private:
    EbAiVisualizerImplementation() = delete;
};

#endif
