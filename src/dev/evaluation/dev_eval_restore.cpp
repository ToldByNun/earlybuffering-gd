#include "dev_eval_restore.hpp"
#include <cstring>
#include <fmt/format.h>

DevWitnessStatus DevEvalRestore::categoryStatus(DevEvalRestore const& r,
                                                DevWitnessCategory candidate) {
    for (auto const& e : r.categories) {
        if (e.category == candidate)
            return e.status;
    }
    return DevWitnessStatus::Absent;
}

void DevEvalRestore::setCategory(DevEvalRestore& r, DevWitnessCategory candidate,
                                 DevWitnessStatus status, uint64_t digest) {
    for (auto& e : r.categories) {
        if (e.category == candidate) {
            e.status = status;
            e.digest = digest;
            return;
        }
    }
    DevWitnessCategoryEntry e;
    e.category = candidate;
    e.status = status;
    e.digest = digest;
    r.categories.push_back(e);
}

DevEvalRestore DevEvalRestore::beginFromWitness(DevLiveWitness const& witness,
                                                DevSimPathId evalPath) {
    DevEvalRestore r;
    r.valid = true;
    r.playerBlobSchema = witness.playerBlobSchema;
    r.freezeWorld = witness.freezeWorld;
    r.evalSimPathId = evalPath;
    r.clock = witness.clock;
    // Clock is bound by contract — mark restored when stamp validates.
    if (witness.clock.valid && DevClockContract::get().clockOk(r.clock)) {
        setCategory(r, DevWitnessCategory::Clock, DevWitnessStatus::Restored);
    } else if (witness.clock.valid) {
        setCategory(r, DevWitnessCategory::Clock, DevWitnessStatus::Partial);
    } else {
        setCategory(r, DevWitnessCategory::Clock, DevWitnessStatus::Absent);
    }
    // Everything else starts absent until note*Written.
    setCategory(r, DevWitnessCategory::Player, DevWitnessStatus::Absent);
    setCategory(r, DevWitnessCategory::Dual, DevWitnessStatus::Absent);
    setCategory(r, DevWitnessCategory::Orbs, DevWitnessStatus::Absent);
    setCategory(r, DevWitnessCategory::Triggers, DevWitnessStatus::Absent);
    setCategory(r, DevWitnessCategory::Movers, DevWitnessStatus::Absent);
    setCategory(r, DevWitnessCategory::CollisionFlags, DevWitnessStatus::Absent);
    return r;
}

void DevEvalRestore::notePlayerWritten(DevEvalRestore& r, DevPlayerBlob const& blob) {
    r.player = blob;
    setCategory(r, DevWitnessCategory::Player,
                blob.present ? DevWitnessStatus::Restored : DevWitnessStatus::Absent);
    if (blob.present) {
        noteCollisionFlagsPartial(r);
    }
}

void DevEvalRestore::noteDualWritten(DevEvalRestore& r, DevPlayerBlob const& blob) {
    r.dual = blob;
    setCategory(r, DevWitnessCategory::Dual,
                blob.present ? DevWitnessStatus::Restored : DevWitnessStatus::Absent);
}

void DevEvalRestore::noteOrbsWritten(DevEvalRestore& r, std::vector<int> const& ids) {
    r.orbIds = ids;
    setCategory(r, DevWitnessCategory::Orbs,
                ids.empty() ? DevWitnessStatus::Partial : DevWitnessStatus::Restored);
}

void DevEvalRestore::noteTriggersSpooled(DevEvalRestore& r, std::string const& storeKey,
                                         int storeVersion, int applyTick) {
    r.triggerStoreKey = storeKey;
    r.triggerStoreVersion = storeVersion;
    r.triggerApplyTick = applyTick;
    r.triggerSpooled = true;
    setCategory(r, DevWitnessCategory::Triggers, DevWitnessStatus::Restored);
}

void DevEvalRestore::noteMoversDigestOnly(DevEvalRestore& r, uint64_t digest) {
    // Hashes must never become Restored.
    setCategory(r, DevWitnessCategory::Movers, DevWitnessStatus::DigestOnly, digest);
}

void DevEvalRestore::noteMoversPosesWritten(DevEvalRestore& r,
                                            std::vector<DevObjectPose> const& poses,
                                            uint64_t digest) {
    r.moverPoses = poses;
    // Poses are restorable — upgrade beyond DigestOnly. Digest kept for drift.
    setCategory(r, DevWitnessCategory::Movers, DevWitnessStatus::Restored, digest);
}

void DevEvalRestore::noteCollisionFlagsPartial(DevEvalRestore& r) {
    setCategory(r, DevWitnessCategory::CollisionFlags, DevWitnessStatus::Partial);
}

bool DevEvalRestore::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += ", ";
        report += name;
    };

    DevLiveWitness w;
    w.valid = true;
    w.playerBlobSchema = DevLiveWitness::computePlayerBlobSchema();
    w.clock = DevClockContract::get().makeStamp(10, false, false, 1.f / 240.f, 1.f / 240.f,
                                                1.f / 240.f, 1.f, DevSimPathId::Plan);
    w.player.present = true;
    w.player.x = 12.f;

    DevEvalRestore result = beginFromWitness(w, DevSimPathId::EvalDelayFromFreeze);
    expect(result.valid, "begin-valid");
    expect(categoryStatus(result, DevWitnessCategory::Clock) == DevWitnessStatus::Restored,
           "begin-clock");
    expect(categoryStatus(result, DevWitnessCategory::Player) == DevWitnessStatus::Absent,
           "begin-player-absent");
    expect(result.evalSimPathId == DevSimPathId::EvalDelayFromFreeze, "begin-path");

    notePlayerWritten(result, w.player);
    expect(categoryStatus(result, DevWitnessCategory::Player) == DevWitnessStatus::Restored,
           "note-player");
    expect(result.player.x == 12.f, "note-player-x");
    expect(categoryStatus(result, DevWitnessCategory::CollisionFlags) == DevWitnessStatus::Partial,
           "note-flags");

    noteMoversDigestOnly(result, 0xdeadull);
    expect(categoryStatus(result, DevWitnessCategory::Movers) == DevWitnessStatus::DigestOnly,
           "movers-digest");

    DevObjectPose pose{};
    pose.uniqueId = 42;
    pose.x = 1.f;
    pose.y = 2.f;
    noteMoversPosesWritten(result, {pose}, 0xdeadull);
    expect(categoryStatus(result, DevWitnessCategory::Movers) == DevWitnessStatus::Restored,
           "movers-poses");
    expect(result.moverPoses.size() == 1 && result.moverPoses[0].uniqueId == 42, "movers-poses-id");

    noteTriggersSpooled(result, "lvl", 1, 11);
    expect(categoryStatus(result, DevWitnessCategory::Triggers) == DevWitnessStatus::Restored,
           "triggers-spool");
    expect(result.triggerSpooled, "triggers-flag");

    noteOrbsWritten(result, {7, 8});
    expect(categoryStatus(result, DevWitnessCategory::Orbs) == DevWitnessStatus::Restored,
           "orbs-written");

    noteDualWritten(result, w.player);
    expect(categoryStatus(result, DevWitnessCategory::Dual) == DevWitnessStatus::Restored,
           "dual-written");

    if (fails == 0) {
        report = "DevEvalRestore selfCheck OK";
        return true;
    }
    report = std::string("DevEvalRestore selfCheck fails: ") + report;
    return false;
}
