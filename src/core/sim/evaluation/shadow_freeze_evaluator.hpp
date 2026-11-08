#ifndef SHADOW_FREEZE_EVALUATOR_HPP
#define SHADOW_FREEZE_EVALUATOR_HPP

#include "../../../dev/capture/dev_live_witness.hpp"
#include "../../../dev/evaluation/dev_eval_restore.hpp"

#include "../shadow_simulator.hpp"

class ShadowFreezeEvaluator {
public:
    static void applyPlayerBlob(PlayerObject* destination, DevPlayerBlob const& blob);
    static void applyEvalWorldRestore(ShadowSimulator& simulator, PlayLayer* layer,
                                      DevLiveWitness const& witness, DevEvalRestore& restore);
    static void finishEvalWorldRestore(ShadowSimulator& simulator, PlayLayer* layer);
    static ShadowSimulator::EvalFromFreezeResult
    evalDelayFromFreeze(ShadowSimulator& simulator, PlayLayer* layer, DevLiveWitness const& witness,
                        int delay, int horizonTicks, bool down, int* tickBudgetLeft);
    static ShadowSimulator::EvalFromFreezeResult
    replayActualFire(ShadowSimulator& simulator, PlayLayer* layer, DevLiveWitness const& witness,
                     int actualFireTickAbs, int horizonTicks, bool down, int* tickBudgetLeft);

private:
    ShadowFreezeEvaluator() = delete;
};

#endif
