#ifndef EARLYBUFFER_UI_TRAJECTORYVISUALIZER_HPP
#define EARLYBUFFER_UI_TRAJECTORYVISUALIZER_HPP

#include <Geode/Geode.hpp>
#include <string>
#include <vector>


// Debug overlay: HOLD vs NOT-HOLD (coast) ghost paths.
// v1: draw length = AssistHorizon trajDraw.
// v2 Live debug: LongForesight horizon (+extend) + yellow prediction windows
// for candidate delays + dark-green best foresight path (13b).
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
    int m_lastDrawCap = -1;   // drawn horizon (may be LF-extended)
    int m_lastBaseCap = -1;   // pre-extend horizon for skip gate
    bool m_trustLong = false;
    bool m_hasWindow = false;
    bool m_v2Live = false;
    bool m_lfExtended = false;
    int m_predCount = 0;
    int m_bestDelay = -1;
};

#endif // EARLYBUFFER_UI_TRAJECTORYVISUALIZER_HPP
