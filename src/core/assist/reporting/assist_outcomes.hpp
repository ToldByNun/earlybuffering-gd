#ifndef ASSIST_OUTCOMES_HPP
#define ASSIST_OUTCOMES_HPP

#include "../../diagnostics/decision_log.hpp"
#include "../../sim/planning/simulation_plan.hpp"
#include "../contracts/assist_verdict.hpp"

#include "../assist_controller.hpp"

class AssistOutcomes {
public:
    static void noteRealJump(AssistController& controller, PlayerObject* player);
    static void recordDecision(AssistController& controller, PlayLayer* playLayer,
                               PlayerObject* player, bool down, SimPlan const& plan, int effectTick,
                               int appliedDelay, int limit);
    static void tickLearning(AssistController& controller, PlayLayer* playLayer,
                             PlayerObject* player);
    static void runPostmortem(AssistController& controller, PlayLayer* playLayer,
                              PlayerObject* player, GameObject* object, DecisionEntry& dec);
    static void beginOutcome(AssistController& controller, SimPlan const& plan, int effectTick);
    static void pollOutcome(AssistController& controller, PlayLayer* playLayer);
    static void logPlan(AssistController& controller, SimPlan const& plan, char const* kind);
    static void noteVerdict(AssistController& controller, AssistReason reason, char const* where);
    static void onRealDeath(AssistController& controller, PlayLayer* playLayer,
                            PlayerObject* player, GameObject* object);

private:
    AssistOutcomes() = delete;
};

#endif
