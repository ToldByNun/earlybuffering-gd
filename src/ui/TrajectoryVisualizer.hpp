#ifndef EARLYBUFFER_UI_TRAJECTORYVISUALIZER_HPP
#define EARLYBUFFER_UI_TRAJECTORYVISUALIZER_HPP

#include <Geode/Geode.hpp>
#include <string>
#include <vector>


// Debug overlay: HOLD vs NOT-HOLD (coast) ghost paths.
// Draw length = AssistHorizon trajDraw (trust cap when SimTrust OK).
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
    int m_lastDrawCap = -1;
    bool m_trustLong = false;
    bool m_hasWindow = false;
};

#endif // EARLYBUFFER_UI_TRAJECTORYVISUALIZER_HPP
