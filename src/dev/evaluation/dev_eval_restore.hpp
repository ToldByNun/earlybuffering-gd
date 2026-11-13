#ifndef DEV_EVAL_RESTORE_HPP
#define DEV_EVAL_RESTORE_HPP

#include "../capture/dev_clock_contract.hpp"
#include "../capture/dev_live_witness.hpp"
#include <cstdint>
#include <string>
#include <vector>

// What evaluation actually wrote into the ghost / world before the first sim tick.
// Commit 8: record + bookkeeping. Ghost writes land in Commit 9.
class EbDevEvalRestore {
public:
    bool valid = false;
    uint64_t playerBlobSchema = 0;
    EbDevPlayerBlob player{};
    EbDevPlayerBlob dual{};
    EbDevClockContract::Stamp clock{};
    std::vector<int> orbIds;
    std::vector<EbDevObjectPose> moverPoses;
    // AP9: contact parity bookkeeping (not applied as pointer refs on ghosts).
    EbDevContactSnap contact{};
    bool hasContact = false;
    EbDevContactSnap dualContact{};
    bool hasDualContact = false;
    std::string triggerStoreKey;
    int triggerStoreVersion = 0;
    int triggerApplyTick = -1;
    bool triggerSpooled = false;
    // Per-category status after the restore attempt.
    std::vector<EbDevWitnessCategoryEntry> categories;
    DevSimPathId evalSimPathId = DevSimPathId::None;
    bool freezeWorld = false;

    EbDevEvalRestore() = default;

    static DevWitnessStatus categoryStatus(EbDevEvalRestore const& r, DevWitnessCategory candidate);

    // Bind clock stamp from witness (always required for a valid restore attempt).
    // Does not claim player/orbs/movers restored — caller marks those after writes.
    static EbDevEvalRestore beginFromWitness(EbDevLiveWitness const& witness, DevSimPathId evalPath);

    static void setCategory(EbDevEvalRestore& r, DevWitnessCategory candidate,
                            DevWitnessStatus status, uint64_t digest = 0);

    // Record that the player (and optional dual) blob was written to the ghost.
    static void notePlayerWritten(EbDevEvalRestore& r, EbDevPlayerBlob const& blob);
    static void noteDualWritten(EbDevEvalRestore& r, EbDevPlayerBlob const& blob);
    static void noteOrbsWritten(EbDevEvalRestore& r, std::vector<int> const& ids);
    static void noteTriggersSpooled(EbDevEvalRestore& r, std::string const& storeKey,
                                    int storeVersion, int applyTick);
    // Movers: hashes stay DigestOnly forever; poses can upgrade to Restored.
    static void noteMoversDigestOnly(EbDevEvalRestore& r, uint64_t digest);
    static void noteMoversPosesWritten(EbDevEvalRestore& r, std::vector<EbDevObjectPose> const& poses,
                                       uint64_t digest = 0);
    static void noteCollisionFlagsPartial(EbDevEvalRestore& r);
    // AP9: stamp contact scalars onto restore (CollisionFlags stays Partial).
    static void noteContactWritten(EbDevEvalRestore& r, EbDevContactSnap const& snap,
                                   bool dual = false);

    static bool selfCheck(std::string& report);

private:
};

#endif
