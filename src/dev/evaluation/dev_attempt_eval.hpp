#ifndef DEV_ATTEMPT_EVAL_HPP
#define DEV_ATTEMPT_EVAL_HPP

#include "../capture/dev_live_witness.hpp"
#include "dev_eval_engine.hpp"
#include "dev_objective.hpp"
#include "dev_state_diff.hpp"
#include <Geode/Geode.hpp>
#include <string>
#include <vector>

class EbDevReport;
class PlayLayer;

// AP8: optional per-tick pose for a sealed eval delay (chosen / bestSurv / bestEval).
class EbDevTrajPoint {
public:
    int tick = 0;
    float x = 0.f;
    float y = 0.f;
    float vy = 0.f;
    bool alive = true;
    bool unknown = false;
    std::string survivalClass;

private:
};

class EbDevTrajCapture {
public:
    std::string role; // chosen | bestSurv | bestEval
    int delay = -1;
    std::string survivalClass;
    std::vector<EbDevTrajPoint> points;

private:
};

// Offline eval seal attached to a Dev Lab attempt (AP3). Additive JSON key "eval".
class EbDevAttemptEval {
public:
    bool valid = false;

    std::string timeBasis;
    int clockSchema = 0;
    std::string liveSimPathId;
    std::string evalSimPathId;
    bool clockMismatch = false;

    DevFidelity fidelity = DevFidelity::NotReproducible;
    bool claimsAllowed = false;
    bool approx = false;
    std::string evalSuppressedReason;

    std::string objectiveId;
    int horizonTicks = 0;
    int tailTicks = 0;
    std::string gridId; // full|band|sparse
    int limit = 0;
    bool objectivePartial = false;

    bool hasBestSurviving = false;
    int bestSurvivingDelay = -1;
    bool hasBestEvaluated = false;
    int bestEvaluatedDelay = -1;

    bool idealEligible = false;
    bool hasIdealDelay = false;
    int idealDelay = -1;
    std::string idealBlockedReason;

    int plannedDelay = -1;
    bool liveVsEvalMismatch = false;
    int livePlannedDelay = -1;

    // AP7: G10 simEstimate class for this sealed eval (also stamped on planFinished).
    std::string simNetClass;

    std::string overlayLine;
    std::vector<EbDevObjective::Sample> samples;

    // AP8: only when dev-record-traj (default off).
    std::vector<EbDevTrajCapture> trajectories;

    static EbDevAttemptEval fromGrid(EbDevEvalEngine::GridReport const& grid,
                                     EbDevReport const& report);
    // Re-run freeze eval for chosen / bestSurv / bestEval with traj samples.
    static void captureOptionalTrajectories(EbDevAttemptEval& eval, PlayLayer* layer,
                                            EbDevLiveWitness const& witness, bool down);
    static matjson::Value toJson(EbDevAttemptEval const& eval);
    static bool fromJson(matjson::Value const& j, EbDevAttemptEval& out, std::string& err);

private:
};

#endif
