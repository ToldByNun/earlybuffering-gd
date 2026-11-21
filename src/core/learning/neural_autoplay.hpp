#ifndef NEURAL_AUTOPLAY_HPP
#define NEURAL_AUTOPLAY_HPP

#include "ai_route_planner.hpp"
#include <algorithm>
#include <string>

class PlayLayer;
class PlayerObject;

// AI Mode self-play: multi-event route planner + Shadow-verified Jump schedule.
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
    char const* lastProbeReason() const {
        return m_lastProbeReason ? m_lastProbeReason : "—";
    }

    static bool selfCheck(std::string& report) {
        int fails = 0;
        auto expect = [&](bool ok, char const* name) {
            if (!ok) {
                ++fails;
                report += std::string("FAIL ") + name + "; ";
            }
        };
        EbNeuralAutoplay bot;
        expect(!bot.enabled(), "off-default");
        bot.setEnabled(true);
        expect(bot.enabled(), "on");
        bot.setTimeWarp(16.f);
        expect(bot.timeWarp() == 16.f, "warp");
        bot.setExploreRate(0.2f);
        expect(bot.exploreRate() > 0.19f && bot.exploreRate() < 0.21f, "explore");
        bot.setEnabled(false);
        expect(!bot.enabled(), "off-again");
        if (fails == 0) {
            report = "NeuralAutoplay OK";
            return true;
        }
        report = std::string("NeuralAutoplay fails: ") + report;
        return false;
    }

private:
    friend class EbServiceStore;

    EbNeuralAutoplay() = default;

    void press(PlayLayer* layer, bool explore);
    void release(PlayLayer* layer);
    int holdTicksForPlayer(PlayLayer* layer) const;
    bool armFromRoute(PlayLayer* layer, bool& exploreOut);
    void applyTimeWarp(PlayLayer* layer);
    void restoreTimeWarp(PlayLayer* layer);
    void stickTimeWarp(PlayLayer* layer);
    void clearSchedule() {
        m_scheduledPressTick = -1;
        m_scheduledExplore = false;
        m_plannedHoldTicks = -1;
    }
    void clearRoute() {
        m_activeRoute.clear();
        m_routeIndex = 0;
    }

    bool m_enabled = false;
    bool m_restartOnDeath = true;
    bool m_holding = false;
    bool m_pendingRestart = false;
    bool m_warpOwned = false;
    bool m_warpNeedsApply = false;
    bool m_warpNeedsRestore = false;
    bool m_scheduledExplore = false;
    int m_holdUntilTick = -1;
    int m_nextPressTick = 0;
    int m_nextProbeTick = 0;
    int m_restartAtTick = -1;
    int m_scheduledPressTick = -1;
    int m_plannedHoldTicks = -1;
    int m_presses = 0;
    int m_explorePresses = 0;
    int m_probes = 0;
    int m_restarts = 0;
    int m_gapTicks = 28;
    int m_probeEveryTicks = 14;
    float m_timeWarp = 8.f;
    float m_savedTimeWarp = 1.f;
    float m_exploreRate = 0.08f;
    char const* m_lastProbeReason = "—";
    EbAiRoute m_activeRoute;
    int m_routeIndex = 0;
};

#endif
