#pragma once

#include "DecisionLog.hpp"
#include "ShadowSimulator.hpp"
#include "../config/StageConfig.hpp"

#include <Geode/Geode.hpp>
#include <string>

namespace frire {

enum class BufferState {
    Idle,
    Buffered,      // action swallowed; waiting until fireTick, then we inject it
    FiredHolding   // legacy — cleared if seen
};

struct BufferedInput {
    BufferState state = BufferState::Idle;
    bool down = true;
    bool player1 = true;
    int pressTick = 0;
    int fireTick = 0;
    int limit = 0;
    int expectedJumpTick = 0;
    bool continuous = false;
    bool releaseSeen = false;
    int releaseTick = 0;
    bool jumpSeen = false;
    bool injected = false;
    int rechecks = 0;
    float decisionX = 0.f;
    GameModeKind mode = GameModeKind::Cube;
};

class AssistController {
public:
    static AssistController& get();

    void resetAttempt(PlayLayer* playLayer = nullptr);
    void onLevelInit(PlayLayer* playLayer);
    void onLevelQuit();

    void onPhysicsTick(PlayLayer* playLayer, float dt);
    void beginRealUpdate();
    void endRealUpdate();
    void onSubStep(PlayerObject* player, float dt);

    bool onHandleButton(GJBaseGameLayer* layer, bool down, int button, bool player1);
    bool allowPushButton(PlayerObject* player, PlayerButton button);

    void noteRealJump(PlayerObject* player);
    void onRealDeath(PlayLayer* playLayer, PlayerObject* player, GameObject* object);

    void resetLevelProfile();

    // Bake map: capture at attempt start; run offline bake from pause UI.
    int startSnapshotSlot() const { return m_startSnapshotSlot; }
    geode::Result<int> runBakeMap(PlayLayer* layer);

    bool isInjecting() const;
    BufferedInput const& buffer() const;
    int attemptTick() const;
    float lastDt() const;
    double estimatedTps() const;

    void updateOverlay();
    std::string overlayText() const;

private:
    AssistController() = default;

    int activeLimit(PlayerObject* player) const;
    bool shouldAssistPlayer(PlayerObject* player) const;
    void clearBuffer();
    void inject(GJBaseGameLayer* layer, bool down);
    void syncButtonState(PlayLayer* playLayer);
    bool planPress(PlayLayer* playLayer, PlayerObject* player, bool player1, int effectTick, float firstDt);
    bool planRelease(PlayLayer* playLayer, PlayerObject* player, bool player1, int effectTick, float firstDt);
    // Returns true if bake handled the input (caller should return that bool).
    // Sets handled=false if bake has no window and Hybrid should fall through to live.
    bool tryBakeAssist(
        PlayLayer* playLayer, PlayerObject* player, bool player1,
        bool down, int effectTick, bool& handled
    );
    void fireBuffered(PlayLayer* playLayer, PlayerObject* player);
    void flushPendingRelease(PlayLayer* playLayer);
    bool userHolding(bool player1) const { return m_userHolding[player1 ? 0 : 1]; }

    void recordDecision(
        PlayLayer* playLayer, PlayerObject* player, bool down,
        SimPlan const& plan, int effectTick, int appliedDelay, int limit
    );
    void tickLearning(PlayLayer* playLayer, PlayerObject* player);
    void runPostmortem(PlayLayer* playLayer, PlayerObject* player, GameObject* object, DecisionEntry& dec);

    BufferedInput m_buffer{};
    DecisionLog m_log{};
    bool m_injecting = false;
    int m_attemptTick = 0;
    float m_lastDt = 1.f / 240.f;
    float m_simDt = 1.f / 240.f;
    bool m_inRealUpdate = false;
    float m_subDtSeen = 0.f;
    bool m_userHolding[2] = { false, false };
    bool m_modeShip = false;
    bool m_modeBall = false;
    bool m_modeBird = false;
    bool m_modeDart = false;
    bool m_modeRobot = false;
    bool m_modeSpider = false;
    bool m_modeSwing = false;
    bool m_modeKnown = false;
    bool m_pendingRelease = false;
    bool m_pendingReleaseP1 = true;
    int m_pendingReleaseTick = 0;
    float m_lastPlayerX = 0.f;
    GameModeKind m_lastMode = GameModeKind::Cube;
    int m_startSnapshotSlot = -1;
    bool m_startSnapshotTaken = false;
    std::string m_lastBakeHint;
    std::string m_overlayText = "frirecracker: OFF";
};

}
