#ifndef NEURAL_AUTOPLAY_HPP
#define NEURAL_AUTOPLAY_HPP

#include "ai_route.hpp"
#include "autoplay_metrics.hpp"
#include <algorithm>
#include <deque>
#include <string>
#include <vector>

class PlayLayer;
class PlayerObject;

// AI Mode self-play: multi-event route planner + Shadow-verified Jump schedule.
// AP-NL6: route queue, early plan, memory reuse, Autoplay-only CPS, DR4 metrics.
class EbNeuralAutoplay {
public:
    static EbNeuralAutoplay& get();

    void setEnabled(bool on) {
        if (on == m_enabled)
            return;
        m_enabled = on;
        if (on) {
            m_warpNeedsApply = true;
        } else {
            m_holding = false;
            m_holdUntilTick = -1;
            m_pendingRestart = false;
            m_restartAtTick = -1;
            m_nextPressTick = 0;
            m_nextProbeTick = 0;
            clearSchedule();
            clearRoute();
            m_routeQueue.clear();
            m_warpNeedsRestore = true;
        }
    }
    bool enabled() const {
        return m_enabled;
    }
    void setRestartOnDeath(bool on) {
        m_restartOnDeath = on;
    }
    bool restartOnDeath() const {
        return m_restartOnDeath;
    }
    void setTimeWarp(float warp) {
        m_timeWarp = std::clamp(warp, 1.f, 32.f);
        if (m_enabled)
            m_warpNeedsApply = true;
    }
    float timeWarp() const {
        return m_timeWarp;
    }
    void setExploreRate(float rate) {
        m_exploreRate = std::clamp(rate, 0.f, 0.25f);
    }
    float exploreRate() const {
        return m_exploreRate;
    }

    // Pull Autoplay knobs from EbAiConfig (presets / UI).
    void syncFromConfig();

    void syncTimeWarp(PlayLayer* layer);
    void onPhysicsTick(PlayLayer* layer);
    void onDeath(PlayLayer* layer);
    void onAttemptReset();

    int presses() const {
        return m_presses;
    }
    int explorePresses() const {
        return m_explorePresses;
    }
    int probes() const {
        return m_probes;
    }
    int restarts() const {
        return m_restarts;
    }
    int routeEvents() const {
        return static_cast<int>(m_activeRoute.events.size());
    }
    int queuedRoutes() const {
        return static_cast<int>(m_routeQueue.size());
    }
    char const* lastProbeReason() const {
        return m_lastProbeReason ? m_lastProbeReason : "—";
    }
    EbAutoplayMetrics const& metrics() const {
        return m_metrics;
    }
    EbAutoplayMetrics& metrics() {
        return m_metrics;
    }

    static bool selfCheck(std::string& report);

private:
    friend class EbServiceStore;

    EbNeuralAutoplay() = default;

    void press(PlayLayer* layer, bool explore);
    void release(PlayLayer* layer);
    int holdTicksForPlayer(PlayLayer* layer) const;
    bool armFromRoute(PlayLayer* layer, bool& exploreOut);
    // Schedule the event at m_routeIndex (Press or Release) by absolute tick.
    bool scheduleNextRouteEvent(PlayLayer* layer);
    bool tryEarlyPlan(PlayLayer* layer);
    bool cpsAllowsPress(int tick) const;
    void recordPressTick(int tick);
    void beginVerifiedRoute(EbAiRoute const& route, float startX, int tick);
    void finishRouteSegment(float liveX, int tick, bool death);
    void applyTimeWarp(PlayLayer* layer);
    void restoreTimeWarp(PlayLayer* layer);
    void stickTimeWarp(PlayLayer* layer);
    void clearSchedule() {
        m_scheduledPressTick = -1;
        m_scheduledReleaseTick = -1;
        m_scheduledExplore = false;
        m_plannedHoldTicks = -1;
    }
    void clearRoute() {
        m_activeRoute.clear();
        m_routeIndex = 0;
        m_verifiedSnapshot.clear();
        m_executedEvents.clear();
        m_routeStartX = 0.f;
        m_routeStartTick = -1;
    }

    bool m_enabled = false;
    bool m_restartOnDeath = true;
    bool m_holding = false;
    bool m_pendingRestart = false;
    bool m_warpOwned = false;
    bool m_warpNeedsApply = false;
    bool m_warpNeedsRestore = false;
    bool m_scheduledExplore = false;
    bool m_memoryReuseRoute = true;
    int m_holdUntilTick = -1;
    int m_nextPressTick = 0;
    int m_nextProbeTick = 0;
    int m_restartAtTick = -1;
    int m_scheduledPressTick = -1;
    int m_scheduledReleaseTick = -1;
    int m_plannedHoldTicks = -1;
    int m_presses = 0;
    int m_explorePresses = 0;
    int m_probes = 0;
    int m_restarts = 0;
    int m_gapTicks = 28;
    int m_probeEveryTicks = 14;
    int m_maxRouteQueue = 2;
    int m_earlyPlanTicks = 8;
    int m_maxAutoplayCps = 12;
    int m_memoryHoldTicks = 48;
    int m_memoryUntilTick = -1;
    int m_routeStartTick = -1;
    int m_lastSeedNova = 0;
    int m_lastSeedEnh = 0;
    int m_lastSeedPost = 0;
    float m_timeWarp = 8.f;
    float m_savedTimeWarp = 1.f;
    float m_exploreRate = 0.08f;
    float m_routeStartX = 0.f;
    char const* m_lastProbeReason = "—";
    EbAiRoute m_activeRoute;
    EbAiRoute m_verifiedSnapshot;
    EbAiRoute m_memoryRoute;
    std::vector<EbAiRouteEvent> m_executedEvents;
    std::deque<EbAiRoute> m_routeQueue;
    std::deque<int> m_recentPressTicks;
    int m_routeIndex = 0;
    EbAutoplayMetrics m_metrics;
};

#endif
