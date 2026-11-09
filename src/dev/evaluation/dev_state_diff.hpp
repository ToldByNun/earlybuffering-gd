#ifndef DEV_STATE_DIFF_HPP
#define DEV_STATE_DIFF_HPP

#include "../capture/dev_live_witness.hpp"
#include "dev_eval_restore.hpp"
#include <string>
#include <vector>

enum class DevFidelity { Reproduced = 0, PlayerOnlyApproximated, NotReproducible };

enum class DevDiffKind { Mismatch = 0, Unrestored, DigestOnly };

class EbDevDiffEntry {
public:
    DevWitnessCategory category = DevWitnessCategory::Player;
    std::string fieldOrObjectId;
    std::string liveValue;
    std::string restoredValue;
    DevDiffKind kind = DevDiffKind::Mismatch;

private:
};

class EbDevClassifyOptions {
public:
    bool requireDeps = true; // DevConfig::evalRequireDeps
    bool freezeWorld = false;
    DevSimPathId expectedEvalPath = DevSimPathId::EvalDelayFromFreeze;

private:
};

class EbDevClassifyResult {
public:
    DevFidelity fidelity = DevFidelity::NotReproducible;
    bool claimsAllowed = false;
    bool approx = false; // true iff PlayerOnlyApproximated
    std::string evalSuppressedReason;
    std::vector<EbDevDiffEntry> diffs;

private:
};

// Vertrag B: compare witness vs restore, emit fidelity + claimsAllowed.
class EbDevStateDiff {
public:
    static char const* fidelityId(DevFidelity frame);
    static bool parseFidelity(std::string const& s, DevFidelity& out);
    static char const* diffKindId(DevDiffKind offset);

    // Field-wise player blob compare; appends mismatch diffs when out != nullptr.
    static bool playerBlobsEqual(EbDevPlayerBlob const& live, EbDevPlayerBlob const& restored,
                                 DevWitnessCategory category, std::vector<EbDevDiffEntry>* outDiffs);

    // Full classify after a restore attempt (Commit 9+ calls this post-write).
    static EbDevClassifyResult classify(EbDevLiveWitness const& witness, EbDevEvalRestore const& restore,
                                      EbDevClassifyOptions const& opts);

    // Pre-sim gate: suppress when the witness alone already forces notReproducible
    // (missing player / bad clock / schema). No restore object required.
    static EbDevClassifyResult classifyWitnessGate(EbDevLiveWitness const& witness,
                                                 EbDevClassifyOptions const& opts);

    static bool selfCheck(std::string& report);

private:
    friend class EbDevFidelityChecks;

    EbDevStateDiff() = delete;

    static bool isHardFailDep(DevWitnessCategory candidate, EbDevClassifyOptions const& opts);
    static void pushDiff(std::vector<EbDevDiffEntry>& diffs, DevWitnessCategory category,
                         char const* field, std::string liveValue, std::string restoredValue,
                         DevDiffKind kind);
};

#endif
