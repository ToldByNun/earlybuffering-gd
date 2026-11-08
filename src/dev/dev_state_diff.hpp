#ifndef DEV_STATE_DIFF_HPP
#define DEV_STATE_DIFF_HPP

#include "dev_eval_restore.hpp"
#include "dev_live_witness.hpp"
#include <string>
#include <vector>

enum class DevFidelity { Reproduced = 0, PlayerOnlyApproximated, NotReproducible };

enum class DevDiffKind { Mismatch = 0, Unrestored, DigestOnly };

class DevDiffEntry {
public:
    DevWitnessCategory category = DevWitnessCategory::Player;
    std::string fieldOrObjectId;
    std::string liveValue;
    std::string restoredValue;
    DevDiffKind kind = DevDiffKind::Mismatch;

private:
};

class DevClassifyOptions {
public:
    bool requireDeps = true; // DevConfig::evalRequireDeps
    bool freezeWorld = false;
    DevSimPathId expectedEvalPath = DevSimPathId::EvalDelayFromFreeze;

private:
};

class DevClassifyResult {
public:
    DevFidelity fidelity = DevFidelity::NotReproducible;
    bool claimsAllowed = false;
    bool approx = false; // true iff PlayerOnlyApproximated
    std::string evalSuppressedReason;
    std::vector<DevDiffEntry> diffs;

private:
};

// Vertrag B: compare witness vs restore, emit fidelity + claimsAllowed.
class DevStateDiff {
public:
    static char const* fidelityId(DevFidelity frame);
    static bool parseFidelity(std::string const& s, DevFidelity& out);
    static char const* diffKindId(DevDiffKind offset);

    // Field-wise player blob compare; appends mismatch diffs when out != nullptr.
    static bool playerBlobsEqual(DevPlayerBlob const& live, DevPlayerBlob const& restored,
                                 DevWitnessCategory category, std::vector<DevDiffEntry>* outDiffs);

    // Full classify after a restore attempt (Commit 9+ calls this post-write).
    static DevClassifyResult classify(DevLiveWitness const& witness, DevEvalRestore const& restore,
                                      DevClassifyOptions const& opts);

    // Pre-sim gate: suppress when the witness alone already forces notReproducible
    // (missing player / bad clock / schema). No restore object required.
    static DevClassifyResult classifyWitnessGate(DevLiveWitness const& witness,
                                                 DevClassifyOptions const& opts);

    static bool selfCheck(std::string& report);

private:
    DevStateDiff() = delete;

    static bool isHardFailDep(DevWitnessCategory candidate, DevClassifyOptions const& opts);
    static void pushDiff(std::vector<DevDiffEntry>& diffs, DevWitnessCategory category,
                         char const* field, std::string liveValue, std::string restoredValue,
                         DevDiffKind kind);
};

#endif
