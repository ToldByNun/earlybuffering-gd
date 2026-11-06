#ifndef EARLYBUFFER_UI_BAKEREPLAYVISUALIZER_HPP
#define EARLYBUFFER_UI_BAKEREPLAYVISUALIZER_HPP

#include "../core/ShadowSimulator.hpp"

#include <Geode/Geode.hpp>
#include <vector>

class BakeReplayVisualizer {
public:
    static BakeReplayVisualizer& get();

    void start(
        PlayLayer* layer,
        std::vector<ShadowSimulator::BakeFrameTrace> frames,
        double tps,
        int firstContactFrame
    );
    void stop();
    void detach(bool layerDying);
    void onPhysicsTick(PlayLayer* layer, float dt);
    bool active() const { return m_active; }

private:
    BakeReplayVisualizer() = default;
    void rebuild();

    cocos2d::CCDrawNode* m_node = nullptr;
    cocos2d::CCLabelBMFont* m_label = nullptr;
    std::vector<ShadowSimulator::BakeFrameTrace> m_frames;
    double m_tps = 240.0;
    double m_cursor = 0.0;
    int m_firstContactFrame = -1;
    bool m_active = false;
};

#endif
