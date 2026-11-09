#ifndef ASSIST_INPUT_GATE_HPP
#define ASSIST_INPUT_GATE_HPP

#include "../../../config/stage_config.hpp"
#include <string>

#include "../assist_controller.hpp"

class EbAssistInputGate {
public:
    static bool continuousModeKind(GameModeKind kind);
    static bool continuousModePlayer(PlayerObject* player);
    static bool releaseAssistModeKind(GameModeKind kind);
    static bool releaseAssistModePlayer(PlayerObject* player);
    static bool shouldPlanRelease(EbAssistController const& controller, PlayLayer* /*playLayer*/,
                                  PlayerObject* player, bool player1, int effectTick);
    static bool releaseGateSelfCheck(std::string& report);
    static int activeLimit(EbAssistController const& controller, PlayerObject* player);
    static bool shouldAssistPlayer(EbAssistController const& controller, PlayerObject* player);
    static void pruneClickTimes(EbAssistController& controller);
    static void noteUserClick(EbAssistController& controller);
    static int currentCps(EbAssistController const& controller);
    static bool cpsLimited(EbAssistController const& controller);

private:
    EbAssistInputGate() = delete;
};

#endif
