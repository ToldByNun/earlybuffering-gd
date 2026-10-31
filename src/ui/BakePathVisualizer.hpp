#ifndef EARLYBUFFER_UI_BAKEPATHVISUALIZER_HPP
#define EARLYBUFFER_UI_BAKEPATHVISUALIZER_HPP

#include <Geode/Geode.hpp>
#include <string>

// Draws BakeMap click windows/path in green (coverage check). Off by default.
// Sibling to TrajectoryVisualizer — no expensive resims; uses stored click.x.
class BakePathVisualizer {
public:
    static BakePathVisualizer& get();

    void attach(PlayLayer* layer);
    void detach(bool layerDying = false);
    void clear();

    void onPhysicsTick(PlayLayer* layer, float dt);

    void setEnabled(bool on);
    bool enabled() const { return m_enabled; }

    std::string statusHint() const;

private:
    BakePathVisualizer() = default;

    void ensureNode(PlayLayer* layer);
    void rebuild(PlayLayer* layer);

    // LOD: only draw clicks within this X of the player (± screens).
    static constexpr float kLodMarginX = 1000.f;
    static constexpr size_t kMaxMarkers = 48;
    static constexpr int kRebuildEveryTicks = 20;

    cocos2d::CCDrawNode* m_node = nullptr;
    bool m_enabled = false;
    int m_ticksSinceRebuild = 0;
    float m_lastRebuildX = -1e9f;
    int m_lastClickCount = -1;
    int m_drawnNear = 0;
};

#endif
