#ifndef EARLYBUFFER_CORE_GHOSTPARITYPROBE_HPP
#define EARLYBUFFER_CORE_GHOSTPARITYPROBE_HPP

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

// Past-window ghost vs live pose check (Commit 1).
// Anchors live in reserved ShadowSimulator parity snapshot slots so plan()
// captures cannot clobber them. ε placeholders until Commit 2 freezes noise.
class GhostParityProbe {
public:
    static constexpr int kPoseCap = 240;
    static constexpr int kHorizonTicks = 48;
    static constexpr int kAnchorEvery = 16;
    static constexpr int kProbeEvery = 16;
    // Placeholder ε — Commit 2 replaces from corpus p99 and freezes.
    static constexpr float kDefaultEpsX = 12.f;
    static constexpr float kDefaultEpsY = 12.f;
    static constexpr int kMaxProbeUs = 900;

    static GhostParityProbe& get();

    void resetAttempt();
    void onLevelQuit();

    // Call once per physics tick after attemptTick is advanced.
    void onPhysicsTick(PlayLayer* layer, PlayerObject* player, float simDt, int attemptTick);

    bool lastProbeDrifted() const { return this->m_lastDrifted; }
    int probeCount() const { return this->m_probeCount; }
    int driftCount() const { return this->m_driftCount; }

    float epsX() const { return this->m_epsX; }
    float epsY() const { return this->m_epsY; }
    // Commit 2 will call this once with frozen corpus values.
    void setEps(float epsX, float epsY);

    static bool selfCheck(std::string& report);

private:
    GhostParityProbe() = default;

    struct PoseSample {
        int tick = 0;
        float x = 0.f;
        float y = 0.f;
        bool jumpHeld = false;
        bool dead = false;
    };

    struct Anchor {
        int tick = -1;
        int slot = -1;
        bool valid = false;
    };

    void recordPose(PlayerObject* player, int attemptTick);
    void maybeCaptureAnchor(PlayerObject* player, int attemptTick);
    void maybeRunProbe(PlayLayer* layer, PlayerObject* player, float simDt, int attemptTick);
    PoseSample const* findPose(int tick) const;

    std::vector<PoseSample> m_poses;
    Anchor m_anchors[2]{};
    int m_nextAnchor = 0;
    float m_epsX = kDefaultEpsX;
    float m_epsY = kDefaultEpsY;
    bool m_lastDrifted = false;
    int m_probeCount = 0;
    int m_driftCount = 0;
};

#endif // EARLYBUFFER_CORE_GHOSTPARITYPROBE_HPP
