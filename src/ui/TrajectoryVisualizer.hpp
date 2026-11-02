#ifndef EARLYBUFFER_UI_TRAJECTORYVISUALIZER_HPP
#define EARLYBUFFER_UI_TRAJECTORYVISUALIZER_HPP

#include <Geode/Geode.hpp>
#include <string>
#include <vector>


// Debug overlay: HOLD vs NOT-HOLD (coast) ghost paths until death
// (TrajDrawHorizon hard max). v2 also draws short yellow delay preds +
// dark-green best Delay-worthy Alive path (preds stay on scoreHorizon).
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
    void drawPredWindow(
        cocos2d::CCDrawNode* node,
        cocos2d::CCPoint const& at,
        cocos2d::ccColor4F stroke,
        cocos2d::ccColor4F fill
    );

    cocos2d::CCDrawNode* m_node = nullptr;
    bool m_enabled = false;
    int m_ticksSinceRebuild = 0;
    float m_lastRebuildX = -1e9f;
    int m_lastDrawCap = -1;    // HOLD/coast until-death max used
    int m_lastDrawnTicks = -1; // max(hold,coast) frames actually drawn
    int m_lastPredCap = -1;    // short pred horizon
    bool m_hitDrawCap = false; // either path reached max without death
    bool m_trustLong = false;
    bool m_hasWindow = false;
    bool m_v2Live = false;
    int m_predCount = 0;
    int m_bestDelay = -1;
};

#endif // EARLYBUFFER_UI_TRAJECTORYVISUALIZER_HPP
