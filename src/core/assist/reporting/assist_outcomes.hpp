#ifndef ASSIST_OUTCOMES_HPP
#define ASSIST_OUTCOMES_HPP

#include "../../diagnostics/decision_log.hpp"
#include "../../sim/planning/simulation_plan.hpp"
#include "../contracts/assist_verdict.hpp"

#include "../assist_controller.hpp"

class EbAssistOutcomes {
public:
    static void noteRealJump(EbAssistController& controller, PlayerObject* player);
    static void recordDecision(EbAssistController& controller, PlayLayer* playLayer,
                               PlayerObject* player, bool down, EbSimPlan const& plan, int effectTick,
                               int appliedDelay, int limit);
    static void tickLearning(EbAssistController& controller, PlayLayer* playLayer,
                             PlayerObject* player);
    static void runPostmortem(EbAssistController& controller, PlayLayer* playLayer,
                              PlayerObject* player, GameObject* object, EbDecisionEntry& dec);
    static void beginOutcome(EbAssistController& controller, EbSimPlan const& plan, int effectTick);
    static void pollOutcome(EbAssistController& controller, PlayLayer* playLayer);
    static void logPlan(EbAssistController& controller, EbSimPlan const& plan, char const* kind);
    static void noteVerdict(EbAssistController& controller, AssistReason reason, char const* where);
    static void onRealDeath(EbAssistController& controller, PlayLayer* playLayer,
                            PlayerObject* player, GameObject* object);

private:
    EbAssistOutcomes() = delete;
};

#endif
