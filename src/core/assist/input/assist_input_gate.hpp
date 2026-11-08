#ifndef ASSIST_INPUT_GATE_HPP
#define ASSIST_INPUT_GATE_HPP

#include "../../../config/stage_config.hpp"
#include <string>

#include "../assist_controller.hpp"

class AssistInputGate {
public:
    static bool continuousModeKind(GameModeKind kind);
    static bool continuousModePlayer(PlayerObject* player);
    static bool releaseAssistModeKind(GameModeKind kind);
    static bool releaseAssistModePlayer(PlayerObject* player);
    static bool shouldPlanRelease(AssistController const& controller, PlayLayer* /*playLayer*/,
                                  PlayerObject* player, bool player1, int effectTick);
    static bool releaseGateSelfCheck(std::string& report);
    static int activeLimit(AssistController const& controller, PlayerObject* player);
    static bool shouldAssistPlayer(AssistController const& controller, PlayerObject* player);
    static void pruneClickTimes(AssistController& controller);
    static void noteUserClick(AssistController& controller);
    static int currentCps(AssistController const& controller);
    static bool cpsLimited(AssistController const& controller);

private:
    AssistInputGate() = delete;
};

#endif
