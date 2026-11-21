#ifndef NEURAL_AUTOPLAY_HPP
#define NEURAL_AUTOPLAY_HPP

#include <string>

class PlayLayer;

// AI-mode self-play driver: synthesizes Jump edges so the Candidate Scorer can
// RL-train without a human. Uses the normal handleButton → assist plan path.
class EbNeuralAutoplay {
public:
    static EbNeuralAutoplay& get();

    void setEnabled(bool on) {
        m_enabled = on;
        if (!on) {
            m_holding = false;
            m_holdUntilTick = -1;
            m_pendingRestart = false;
            m_restartAtTick = -1;
            m_nextPressTick = 0;
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

    void onPhysicsTick(PlayLayer* layer);
    void onDeath(PlayLayer* layer);
    void onAttemptReset();

    int presses() const {
        return m_presses;
    }
    int restarts() const {
        return m_restarts;
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

    void press(PlayLayer* layer);
    void release(PlayLayer* layer);
    int holdTicksForPlayer(PlayLayer* layer) const;

    bool m_enabled = false;
    bool m_restartOnDeath = true;
    bool m_holding = false;
    bool m_pendingRestart = false;
    int m_holdUntilTick = -1;
    int m_nextPressTick = 0;
    int m_restartAtTick = -1;
    int m_presses = 0;
    int m_restarts = 0;
    int m_gapTicks = 5;
};

#endif
