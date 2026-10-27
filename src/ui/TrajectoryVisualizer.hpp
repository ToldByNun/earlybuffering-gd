#ifndef EARLYBUFFER_UI_TRAJECTORYVISUALIZER_HPP
#define EARLYBUFFER_UI_TRAJECTORYVISUALIZER_HPP

#include <Geode/Geode.hpp>
#include <string>
#include <vector>


// Lean debug overlay: two predicted paths (early vs late click) + fill band.
class TrajectoryVisualizer {
public:
    static TrajectoryVisualizer& get();

    void attach(PlayLayer* layer);
    void detach(bool layerDying = false);
    void clear();

    void onPhysicsTick(PlayLayer* layer, float dt);

    void setEnabled(bool on);
    bool enabled() const { return m_enabled; }
    void toggle();

    std::string statusHint() const;

private:
    TrajectoryVisualizer() = default;

    void ensureNode(PlayLayer* layer);
    void rebuild(PlayLayer* layer);
    void drawPath(
        cocos2d::CCDrawNode* node,
        std::vector<cocos2d::CCPoint> const& pts,
        cocos2d::ccColor4F color,
        float width,
        int stride = 1
    );
    void drawBand(
        cocos2d::CCDrawNode* node,
        std::vector<cocos2d::CCPoint> const& early,
        std::vector<cocos2d::CCPoint> const& late
    );

    cocos2d::CCDrawNode* m_node = nullptr;
    bool m_enabled = false;
    int m_ticksSinceRebuild = 0;
    float m_lastRebuildX = -1e9f;
    int m_lastDelayEarly = -1;
    int m_lastDelayLate = -1;
    float m_lastWindowEarly = 0.f;
    float m_lastWindowLate = 0.f;
    bool m_hasWindow = false;
    bool m_fromBake = false;
};

#endif // EARLYBUFFER_UI_TRAJECTORYVISUALIZER_HPP
