#ifndef EARLYBUFFER_UI_BAKEPATHVISUALIZER_HPP
#define EARLYBUFFER_UI_BAKEPATHVISUALIZER_HPP

#include "../config/StageConfig.hpp"

#include <Geode/Geode.hpp>
#include <string>

// Draws BakeMap windows near the player (coverage check). Off by default.
// Green = press. Purple = release, gated (wave/ship/swing/robot, else holdAssist).
// Windows only — no path connector between clicks. Sibling to TrajectoryVisualizer;
// no expensive resims; uses stored click.x.
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

    // Same gate as purple viz / upcoming release assist: wave/ship/swing/robot.
    static bool releaseWindowMode(GameModeKind kind);

private:
    BakePathVisualizer() = default;

    void ensureNode(PlayLayer* layer);
    void rebuild(PlayLayer* layer);

    // LOD: only draw clicks within this X of the player (± screens).
    static constexpr float kLodMarginX = 1000.f;
    static constexpr size_t kMaxMarkers = 48;
    // Faster rebuild so windows dim/drop soon after latest (time-based, not only X).
    static constexpr int kRebuildEveryTicks = 8;

    cocos2d::CCDrawNode* m_node = nullptr;
    bool m_enabled = false;
    int m_ticksSinceRebuild = 0;
    float m_lastRebuildX = -1e9f;
    int m_lastClickCount = -1;
    int m_lastApproxFrame = -1;
    GameModeKind m_lastMode = GameModeKind::Cube;
    int m_drawnPress = 0;
    int m_drawnRelease = 0;
    int m_drawnRoute = 0;
};

#endif
