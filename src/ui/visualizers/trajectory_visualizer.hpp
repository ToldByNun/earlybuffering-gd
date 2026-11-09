#ifndef TRAJECTORY_VISUALIZER_HPP
#define TRAJECTORY_VISUALIZER_HPP

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

// Debug overlay: HOLD vs NOT-HOLD (coast) ghost paths.
// v1: draw length = AssistHorizon trajDraw.
// v2 Live debug: LongForesight horizon (+extend) + yellow prediction windows
// for candidate delays + dark-green best foresight path (13b).
class EbTrajectoryVisualizer {
public:
    static EbTrajectoryVisualizer& get();

    void attach(PlayLayer* layer);
    void detach(bool layerDying = false);
    void clear();

    void onPhysicsTick(PlayLayer* layer, float deltaTime);

    void setEnabled(bool on);
    bool enabled() const {
        return m_enabled;
    }
    void toggle();

    std::string statusHint() const;

private:
    friend class EbServiceStore;

    EbTrajectoryVisualizer() = default;

    void ensureNode(PlayLayer* layer);
    void rebuild(PlayLayer* layer);
    void drawPath(cocos2d::CCDrawNode* node, std::vector<cocos2d::CCPoint> const& pts,
                  cocos2d::ccColor4F color, float width, int stride = 1);
    void drawBand(cocos2d::CCDrawNode* node, std::vector<cocos2d::CCPoint> const& early,
                  std::vector<cocos2d::CCPoint> const& late);
    void drawPredWindow(cocos2d::CCDrawNode* node, cocos2d::CCPoint const& at,
                        cocos2d::ccColor4F stroke, cocos2d::ccColor4F fill);

    cocos2d::CCDrawNode* m_node = nullptr;
    bool m_enabled = false;
    int m_ticksSinceRebuild = 0;
    float m_lastRebuildX = -1e9f;
    int m_lastDrawCap = -1; // drawn horizon (may be LF-extended)
    int m_lastBaseCap = -1; // pre-extend horizon for skip gate
    bool m_trustLong = false;
    bool m_hasWindow = false;
    bool m_v2Live = false;
    bool m_lfExtended = false;
    int m_predCount = 0;
    int m_bestDelay = -1;
};

#include "../../config/stage_config.hpp"
#include "../../core/assist/assist_clock.hpp"
#include "../../core/assist/assist_controller.hpp"
#include "../../core/assist/planning/assist_horizon.hpp"
#include "../../core/assist/planning/assist_pick_v2.hpp"
#include "../../core/learning/level_profile.hpp"
#include "../../core/sim/foresight/long_foresight.hpp"
#include "../../core/sim/shadow_simulator.hpp"
#include "../../core/sim/sim_trust.hpp"
#include "../../debug/assist_log.hpp"
#include "../../debug/perf_trace.hpp"
#include <algorithm>
#include <cmath>

class EbTrajectoryVisualizerImplementation {
public:
    static geode::prelude::ccColor4F col(float result, float gameObject, float secondValue,
                                         float firstValue = 0.95f);
    static std::vector<EbReplayEvent> makeHoldEvents();
    static std::vector<EbReplayEvent> makeCoastEvents();
    static std::vector<EbReplayEvent> makeDelayedHoldEvents(int delay);
    static std::vector<geode::prelude::CCPoint>
    decimate(std::vector<geode::prelude::CCPoint> const& pts, float minDistSq);
    static void markDeathX(geode::prelude::CCDrawNode* node, EbShadowSimulator::PathSample const& s,
                           geode::prelude::ccColor4F candidate);
    static int surviveBinOfTicks(int ticks);

private:
    EbTrajectoryVisualizerImplementation() = delete;
};

#endif
