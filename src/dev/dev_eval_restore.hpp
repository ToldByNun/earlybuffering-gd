#ifndef DEV_EVAL_RESTORE_HPP
#define DEV_EVAL_RESTORE_HPP

#include "dev_clock_contract.hpp"
#include "dev_live_witness.hpp"
#include <cstdint>
#include <string>
#include <vector>

// What evaluation actually wrote into the ghost / world before the first sim tick.
// Commit 8: record + bookkeeping. Ghost writes land in Commit 9.
class DevEvalRestore {
public:
    bool valid = false;
    uint64_t playerBlobSchema = 0;
    DevPlayerBlob player{};
    DevPlayerBlob dual{};
    DevClockContract::Stamp clock{};
    std::vector<int> orbIds;
    std::vector<DevObjectPose> moverPoses;
    std::string triggerStoreKey;
    int triggerStoreVersion = 0;
    int triggerApplyTick = -1;
    bool triggerSpooled = false;
    // Per-category status after the restore attempt.
    std::vector<DevWitnessCategoryEntry> categories;
    DevSimPathId evalSimPathId = DevSimPathId::None;
    bool freezeWorld = false;

    DevEvalRestore() = default;

    static DevWitnessStatus categoryStatus(DevEvalRestore const& r, DevWitnessCategory candidate);

    // Bind clock stamp from witness (always required for a valid restore attempt).
    // Does not claim player/orbs/movers restored — caller marks those after writes.
    static DevEvalRestore beginFromWitness(DevLiveWitness const& witness, DevSimPathId evalPath);

    static void setCategory(DevEvalRestore& r, DevWitnessCategory candidate,
                            DevWitnessStatus status, uint64_t digest = 0);

    // Record that the player (and optional dual) blob was written to the ghost.
    static void notePlayerWritten(DevEvalRestore& r, DevPlayerBlob const& blob);
    static void noteDualWritten(DevEvalRestore& r, DevPlayerBlob const& blob);
    static void noteOrbsWritten(DevEvalRestore& r, std::vector<int> const& ids);
    static void noteTriggersSpooled(DevEvalRestore& r, std::string const& storeKey,
                                    int storeVersion, int applyTick);
    // Movers: hashes stay DigestOnly forever; poses can upgrade to Restored.
    static void noteMoversDigestOnly(DevEvalRestore& r, uint64_t digest);
    static void noteMoversPosesWritten(DevEvalRestore& r, std::vector<DevObjectPose> const& poses,
                                       uint64_t digest = 0);
    static void noteCollisionFlagsPartial(DevEvalRestore& r);

    static bool selfCheck(std::string& report);

private:
};

#endif
