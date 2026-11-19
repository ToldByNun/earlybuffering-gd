#ifndef STRAIGHTFLY_VISUALIZER_HPP
#define STRAIGHTFLY_VISUALIZER_HPP

#include <Geode/Geode.hpp>
#include <string>

// Draws the acquired Straight Flight target line (AP03/AP05 diagnostics).
// Sibling to TrajectoryVisualizer — no resims; reads AssistController view.
class EbStraightflyVisualizer {
public:
    static EbStraightflyVisualizer& get();

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

    EbStraightflyVisualizer() = default;

    void ensureNode(PlayLayer* layer);
    void rebuild(PlayLayer* layer);

    static constexpr int kRebuildEveryTicks = 2;
    static constexpr float kForwardLength = 720.f;
    static constexpr float kBackLength = 180.f;

    cocos2d::CCDrawNode* m_node = nullptr;
    bool m_enabled = false;
    int m_ticksSinceRebuild = 0;
    int m_drawnLines = 0;
};

class EbStraightflyVisualizerImplementation {
public:
    static geode::prelude::ccColor4F lineColor(float alpha);
    static geode::prelude::ccColor4F bandColor(float alpha);
    static geode::prelude::ccColor4F anchorColor(float alpha);
    static void axis(bool sideways, bool reversed, double degrees, double& axisX, double& axisY);

private:
    EbStraightflyVisualizerImplementation() = delete;
};

#endif
