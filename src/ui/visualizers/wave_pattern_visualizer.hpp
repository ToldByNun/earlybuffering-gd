#ifndef WAVE_PATTERN_VISUALIZER_HPP
#define WAVE_PATTERN_VISUALIZER_HPP

#include <Geode/Geode.hpp>
#include <string>

// Draws the locked Wave hold/release zigzag and upcoming click edges (AP07 viz).
// Distinct from Straightfly: path + next clicks, not a single corridor line.
class EbWavePatternVisualizer {
public:
    static EbWavePatternVisualizer& get();

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

    EbWavePatternVisualizer() = default;

    void ensureNode(PlayLayer* layer);
    void rebuild(PlayLayer* layer);

    static constexpr int kRebuildEveryTicks = 2;
    static constexpr int kMaxEdges = 10;
    static constexpr float kMaxPathLength = 900.f;

    cocos2d::CCDrawNode* m_node = nullptr;
    bool m_enabled = false;
    int m_ticksSinceRebuild = 0;
    int m_drawnPaths = 0;
    int m_drawnEdges = 0;
};

class EbWavePatternVisualizerImplementation {
public:
    static geode::prelude::ccColor4F pathColor(float alpha);
    static geode::prelude::ccColor4F nextClickColor(float alpha);
    static geode::prelude::ccColor4F laterClickColor(float alpha);
    static geode::prelude::ccColor4F ghostColor(float alpha);
    static void stepDirection(bool holding, bool sideways, bool reversed, bool inverted,
                              double& dirX, double& dirY);

private:
    EbWavePatternVisualizerImplementation() = delete;
};

#endif
