#ifndef SHADOW_FREEZE_EVALUATOR_HPP
#define SHADOW_FREEZE_EVALUATOR_HPP

#include "../../../dev/capture/dev_live_witness.hpp"
#include "../../../dev/evaluation/dev_eval_restore.hpp"

#include "../shadow_simulator.hpp"

class EbShadowFreezeEvaluator {
public:
    static void applyPlayerBlob(PlayerObject* destination, EbDevPlayerBlob const& blob);
    static void applyEvalWorldRestore(EbShadowSimulator& simulator, PlayLayer* layer,
                                      EbDevLiveWitness const& witness, EbDevEvalRestore& restore);
    static void finishEvalWorldRestore(EbShadowSimulator& simulator, PlayLayer* layer);
    static EbShadowSimulator::EvalFromFreezeResult
    evalDelayFromFreeze(EbShadowSimulator& simulator, PlayLayer* layer, EbDevLiveWitness const& witness,
                        int delay, int horizonTicks, bool down, int* tickBudgetLeft);
    static EbShadowSimulator::EvalFromFreezeResult
    replayActualFire(EbShadowSimulator& simulator, PlayLayer* layer, EbDevLiveWitness const& witness,
                     int actualFireTickAbs, int horizonTicks, bool down, int* tickBudgetLeft);

private:
    EbShadowFreezeEvaluator() = delete;
};

#endif
