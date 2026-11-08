#include "dev_state_diff.hpp"
#include <cmath>
#include <cstring>
#include <fmt/format.h>

char const* DevStateDiff::fidelityId(DevFidelity frame) {
    switch (frame) {
    case DevFidelity::Reproduced:
        return "reproduced";
    case DevFidelity::PlayerOnlyApproximated:
        return "playerOnlyApproximated";
    case DevFidelity::NotReproducible:
        return "notReproducible";
    }
    return "notReproducible";
}

bool DevStateDiff::parseFidelity(std::string const& s, DevFidelity& out) {
    if (s == "reproduced") {
        out = DevFidelity::Reproduced;
        return true;
    }
    if (s == "playerOnlyApproximated") {
        out = DevFidelity::PlayerOnlyApproximated;
        return true;
    }
    if (s == "notReproducible") {
        out = DevFidelity::NotReproducible;
        return true;
    }
    return false;
}

char const* DevStateDiff::diffKindId(DevDiffKind offset) {
    switch (offset) {
    case DevDiffKind::Mismatch:
        return "mismatch";
    case DevDiffKind::Unrestored:
        return "unrestored";
    case DevDiffKind::DigestOnly:
        return "digestOnly";
    }
    return "mismatch";
}

void DevStateDiff::pushDiff(std::vector<DevDiffEntry>& diffs, DevWitnessCategory category,
                            char const* field, std::string liveValue, std::string restoredValue,
                            DevDiffKind kind) {
    DevDiffEntry e;
    e.category = category;
    e.fieldOrObjectId = field ? field : "";
    e.liveValue = std::move(liveValue);
    e.restoredValue = std::move(restoredValue);
    e.kind = kind;
    diffs.push_back(std::move(e));
}

bool DevStateDiff::isHardFailDep(DevWitnessCategory candidate, DevClassifyOptions const& opts) {
    if (!opts.requireDeps)
        return false;
    switch (candidate) {
    case DevWitnessCategory::Dual:
    case DevWitnessCategory::Triggers:
    case DevWitnessCategory::Clock:
        return true;
    case DevWitnessCategory::Movers:
        // Without freeze-world, movers digest/absent is soft → approx.
        return opts.freezeWorld;
    case DevWitnessCategory::Player:
    case DevWitnessCategory::Orbs:
    case DevWitnessCategory::CollisionFlags:
        return false;
    }
    return false;
}

bool DevStateDiff::playerBlobsEqual(DevPlayerBlob const& live, DevPlayerBlob const& restored,
                                    DevWitnessCategory category,
                                    std::vector<DevDiffEntry>* outDiffs) {
    if (!live.present || !restored.present) {
        if (outDiffs) {
            pushDiff(*outDiffs, category, "present", live.present ? "1" : "0",
                     restored.present ? "1" : "0", DevDiffKind::Mismatch);
        }
        return false;
    }

    bool ok = true;
    auto checkF = [&](char const* name, float firstValue, float secondValue) {
        if (std::fabs(firstValue - secondValue) <= 1e-4f)
            return;
        ok = false;
        if (outDiffs) {
            pushDiff(*outDiffs, category, name, fmt::format("{}", firstValue),
                     fmt::format("{}", secondValue), DevDiffKind::Mismatch);
        }
    };
    auto checkB = [&](char const* name, bool firstValue, bool secondValue) {
        if (firstValue == secondValue)
            return;
        ok = false;
        if (outDiffs) {
            pushDiff(*outDiffs, category, name, firstValue ? "1" : "0", secondValue ? "1" : "0",
                     DevDiffKind::Mismatch);
        }
    };

    checkF("x", live.x, restored.x);
    checkF("y", live.y, restored.y);
    checkF("rotation", live.rotation, restored.rotation);
    checkF("scaleX", live.scaleX, restored.scaleX);
    checkF("scaleY", live.scaleY, restored.scaleY);
    checkF("yVelocity", live.yVelocity, restored.yVelocity);
    checkF("fallSpeed", live.fallSpeed, restored.fallSpeed);
    checkF("gravity", live.gravity, restored.gravity);
    checkF("gravityMod", live.gravityMod, restored.gravityMod);
    checkF("speedMultiplier", live.speedMultiplier, restored.speedMultiplier);
    checkF("playerSpeed", live.playerSpeed, restored.playerSpeed);
    checkF("vehicleSize", live.vehicleSize, restored.vehicleSize);
    checkF("platformerXVelocity", live.platformerXVelocity, restored.platformerXVelocity);
    checkB("isUpsideDown", live.isUpsideDown, restored.isUpsideDown);
    checkB("isSideways", live.isSideways, restored.isSideways);
    checkB("isGoingLeft", live.isGoingLeft, restored.isGoingLeft);
    checkB("isShip", live.isShip, restored.isShip);
    checkB("isBall", live.isBall, restored.isBall);
    checkB("isBird", live.isBird, restored.isBird);
    checkB("isSwing", live.isSwing, restored.isSwing);
    checkB("isDart", live.isDart, restored.isDart);
    checkB("isRobot", live.isRobot, restored.isRobot);
    checkB("isSpider", live.isSpider, restored.isSpider);
    checkB("isSecondPlayer", live.isSecondPlayer, restored.isSecondPlayer);
    checkB("isPlatformer", live.isPlatformer, restored.isPlatformer);
    checkB("isDashing", live.isDashing, restored.isDashing);
    checkB("isOnGround", live.isOnGround, restored.isOnGround);
    checkB("isOnGround2", live.isOnGround2, restored.isOnGround2);
    checkB("isOnSlope", live.isOnSlope, restored.isOnSlope);
    checkB("wasOnSlope", live.wasOnSlope, restored.wasOnSlope);
    checkB("jumpBuffered", live.jumpBuffered, restored.jumpBuffered);
    checkB("wasJumpBuffered", live.wasJumpBuffered, restored.wasJumpBuffered);
    checkB("stateRingJump", live.stateRingJump, restored.stateRingJump);
    checkB("hasEverJumped", live.hasEverJumped, restored.hasEverJumped);
    checkB("hasEverHitRing", live.hasEverHitRing, restored.hasEverHitRing);
    checkB("isLocked", live.isLocked, restored.isLocked);
    checkB("controlsDisabled", live.controlsDisabled, restored.controlsDisabled);
    checkB("maybeIsFalling", live.maybeIsFalling, restored.maybeIsFalling);
    checkB("maybeIsBoosted", live.maybeIsBoosted, restored.maybeIsBoosted);
    return ok;
}

DevClassifyResult DevStateDiff::classifyWitnessGate(DevLiveWitness const& witness,
                                                    DevClassifyOptions const& opts) {
    (void)opts;
    DevClassifyResult out;
    out.fidelity = DevFidelity::NotReproducible;
    out.claimsAllowed = false;
    out.approx = false;

    if (!witness.valid) {
        out.evalSuppressedReason = "witnessInvalid";
        return out;
    }
    if (!witness.player.present ||
        DevLiveWitness::categoryStatus(witness, DevWitnessCategory::Player) ==
            DevWitnessStatus::Absent) {
        out.evalSuppressedReason = "missingPlayer";
        pushDiff(out.diffs, DevWitnessCategory::Player, "blob", "absent", "",
                 DevDiffKind::Unrestored);
        return out;
    }
    uint64_t const expectSchema = DevLiveWitness::computePlayerBlobSchema();
    if (witness.playerBlobSchema != 0 && witness.playerBlobSchema != expectSchema) {
        out.evalSuppressedReason = "playerSchema";
        pushDiff(out.diffs, DevWitnessCategory::Player, "playerBlobSchema",
                 fmt::format("{:016x}", witness.playerBlobSchema),
                 fmt::format("{:016x}", expectSchema), DevDiffKind::Mismatch);
        return out;
    }
    if (!witness.clock.valid || !DevClockContract::get().clockOk(witness.clock) ||
        DevLiveWitness::categoryStatus(witness, DevWitnessCategory::Clock) ==
            DevWitnessStatus::Absent) {
        out.evalSuppressedReason = "clock";
        pushDiff(out.diffs, DevWitnessCategory::Clock, "stamp", "bad", "", DevDiffKind::Unrestored);
        return out;
    }

    // Witness alone is not enough for claims — need a restore attempt.
    // Gate only suppresses hard failures; otherwise leave room for eval.
    out.fidelity = DevFidelity::NotReproducible;
    out.claimsAllowed = false;
    out.evalSuppressedReason.clear();
    return out;
}

DevClassifyResult DevStateDiff::classify(DevLiveWitness const& witness,
                                         DevEvalRestore const& restore,
                                         DevClassifyOptions const& opts) {
    DevClassifyResult out;

    // Witness-level hard fails first.
    DevClassifyResult gate = classifyWitnessGate(witness, opts);
    if (!gate.evalSuppressedReason.empty()) {
        return gate;
    }

    if (!restore.valid) {
        out.fidelity = DevFidelity::NotReproducible;
        out.claimsAllowed = false;
        out.evalSuppressedReason = "restoreMissing";
        return out;
    }

    if (restore.playerBlobSchema != 0 && witness.playerBlobSchema != 0 &&
        restore.playerBlobSchema != witness.playerBlobSchema) {
        out.fidelity = DevFidelity::NotReproducible;
        out.claimsAllowed = false;
        out.evalSuppressedReason = "playerSchema";
        pushDiff(out.diffs, DevWitnessCategory::Player, "playerBlobSchema",
                 fmt::format("{:016x}", witness.playerBlobSchema),
                 fmt::format("{:016x}", restore.playerBlobSchema), DevDiffKind::Mismatch);
        return out;
    }

    // Clock contract on restore stamp.
    if (!restore.clock.valid || !DevClockContract::get().clockOk(restore.clock) ||
        DevEvalRestore::categoryStatus(restore, DevWitnessCategory::Clock) !=
            DevWitnessStatus::Restored) {
        out.fidelity = DevFidelity::NotReproducible;
        out.claimsAllowed = false;
        out.evalSuppressedReason = "clock";
        pushDiff(out.diffs, DevWitnessCategory::Clock, "stamp", "witness", "bad-or-unrestored",
                 DevDiffKind::Unrestored);
        return out;
    }
    // Stamp identity: effectTick / attemptTick / simDt / midStep / continuous.
    if (restore.clock.effectTick != witness.clock.effectTick ||
        restore.clock.attemptTick != witness.clock.attemptTick ||
        restore.clock.midStep != witness.clock.midStep ||
        restore.clock.continuous != witness.clock.continuous ||
        std::fabs(restore.clock.simDt - witness.clock.simDt) > 1e-9f) {
        out.fidelity = DevFidelity::NotReproducible;
        out.claimsAllowed = false;
        out.evalSuppressedReason = "clockMismatch";
        pushDiff(out.diffs, DevWitnessCategory::Clock, "stampFields", "witness", "restore",
                 DevDiffKind::Mismatch);
        return out;
    }

    if (restore.evalSimPathId != opts.expectedEvalPath) {
        out.fidelity = DevFidelity::NotReproducible;
        out.claimsAllowed = false;
        out.evalSuppressedReason = "evalSimPath";
        pushDiff(out.diffs, DevWitnessCategory::Clock, "evalSimPathId",
                 DevClockContract::simPathId(opts.expectedEvalPath),
                 DevClockContract::simPathId(restore.evalSimPathId), DevDiffKind::Mismatch);
        return out;
    }

    // Player must be restored and match.
    if (DevEvalRestore::categoryStatus(restore, DevWitnessCategory::Player) !=
        DevWitnessStatus::Restored) {
        out.fidelity = DevFidelity::NotReproducible;
        out.claimsAllowed = false;
        out.evalSuppressedReason = "missingPlayer";
        pushDiff(out.diffs, DevWitnessCategory::Player, "blob", "witness", "unrestored",
                 DevDiffKind::Unrestored);
        return out;
    }
    if (!playerBlobsEqual(witness.player, restore.player, DevWitnessCategory::Player, &out.diffs)) {
        out.fidelity = DevFidelity::NotReproducible;
        out.claimsAllowed = false;
        out.evalSuppressedReason = "playerMismatch";
        return out;
    }

    bool allDepsRestored = true;
    bool softFail = false;
    bool hardFail = false;

    for (int depInt : witness.worldDeps) {
        auto const dep = static_cast<DevWitnessCategory>(depInt);
        if (dep == DevWitnessCategory::Player || dep == DevWitnessCategory::Clock) {
            continue; // already verified
        }

        DevWitnessStatus const state = DevEvalRestore::categoryStatus(restore, dep);

        if (dep == DevWitnessCategory::Movers) {
            // Digest/hash alone never counts as Restored.
            // Poses (freeze-world) may be Restored and must match.
            if (state == DevWitnessStatus::Restored) {
                if (witness.moverPoses != restore.moverPoses) {
                    allDepsRestored = false;
                    pushDiff(out.diffs, dep, "poses",
                             fmt::format("n={}", witness.moverPoses.size()),
                             fmt::format("n={}", restore.moverPoses.size()), DevDiffKind::Mismatch);
                    if (isHardFailDep(dep, opts))
                        hardFail = true;
                    else
                        softFail = true;
                }
            } else if (state == DevWitnessStatus::DigestOnly || state == DevWitnessStatus::Absent ||
                       state == DevWitnessStatus::Partial) {
                allDepsRestored = false;
                pushDiff(out.diffs, dep, "movers",
                         DevLiveWitness::statusId(DevLiveWitness::categoryStatus(witness, dep)),
                         DevLiveWitness::statusId(state),
                         state == DevWitnessStatus::DigestOnly ? DevDiffKind::DigestOnly
                                                               : DevDiffKind::Unrestored);
                if (isHardFailDep(dep, opts))
                    hardFail = true;
                else
                    softFail = true;
            }
            continue;
        }

        if (dep == DevWitnessCategory::Dual) {
            if (state != DevWitnessStatus::Restored) {
                allDepsRestored = false;
                pushDiff(out.diffs, dep, "blob", "witness", DevLiveWitness::statusId(state),
                         DevDiffKind::Unrestored);
                if (isHardFailDep(dep, opts))
                    hardFail = true;
                else
                    softFail = true;
            } else if (!playerBlobsEqual(witness.dual, restore.dual, dep, &out.diffs)) {
                allDepsRestored = false;
                hardFail = true; // dual mismatch is fatal for reproduced
                out.evalSuppressedReason = "dualMismatch";
            }
            continue;
        }

        if (dep == DevWitnessCategory::Orbs) {
            if (state != DevWitnessStatus::Restored) {
                allDepsRestored = false;
                pushDiff(out.diffs, dep, "orbIds", fmt::format("{}", witness.orbIds.size()),
                         DevLiveWitness::statusId(state), DevDiffKind::Unrestored);
                softFail = true; // orbs are soft under requireDeps
            } else if (witness.orbIds != restore.orbIds) {
                allDepsRestored = false;
                softFail = true;
                pushDiff(out.diffs, dep, "orbIds", fmt::format("n={}", witness.orbIds.size()),
                         fmt::format("n={}", restore.orbIds.size()), DevDiffKind::Mismatch);
            }
            continue;
        }

        if (dep == DevWitnessCategory::Triggers) {
            if (state != DevWitnessStatus::Restored) {
                allDepsRestored = false;
                pushDiff(out.diffs, dep, "spool", witness.triggerCanSpool ? "canSpool" : "noSpool",
                         DevLiveWitness::statusId(state), DevDiffKind::Unrestored);
                if (isHardFailDep(dep, opts))
                    hardFail = true;
                else
                    softFail = true;
            }
            continue;
        }

        if (dep == DevWitnessCategory::CollisionFlags) {
            // Witness is Partial (pointer refs absent); restore Partial matches.
            if (state == DevWitnessStatus::Absent) {
                allDepsRestored = false;
                softFail = true;
                pushDiff(out.diffs, dep, "flags", "partial", "absent", DevDiffKind::Unrestored);
            } else if (state != DevWitnessStatus::Restored && state != DevWitnessStatus::Partial) {
                allDepsRestored = false;
                softFail = true;
            }
            continue;
        }

        if (state != DevWitnessStatus::Restored) {
            allDepsRestored = false;
            pushDiff(out.diffs, dep, "status", "restored", DevLiveWitness::statusId(state),
                     DevDiffKind::Unrestored);
            if (isHardFailDep(dep, opts))
                hardFail = true;
            else
                softFail = true;
        }
    }

    if (hardFail) {
        out.fidelity = DevFidelity::NotReproducible;
        out.claimsAllowed = false;
        if (out.evalSuppressedReason.empty()) {
            out.evalSuppressedReason = "requireDeps";
        }
        return out;
    }

    if (allDepsRestored && out.diffs.empty()) {
        out.fidelity = DevFidelity::Reproduced;
        out.claimsAllowed = true;
        out.approx = false;
        out.evalSuppressedReason.clear();
        return out;
    }

    // Soft gaps with player+clock ok → approximated.
    if (softFail || !allDepsRestored) {
        out.fidelity = DevFidelity::PlayerOnlyApproximated;
        out.claimsAllowed = true;
        out.approx = true;
        out.evalSuppressedReason.clear();
        return out;
    }

    out.fidelity = DevFidelity::NotReproducible;
    out.claimsAllowed = false;
    if (out.evalSuppressedReason.empty()) {
        out.evalSuppressedReason = "notReproducible";
    }
    return out;
}

bool DevStateDiff::selfCheck(std::string& report) {
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

    expect(std::strcmp(fidelityId(DevFidelity::Reproduced), "reproduced") == 0, "fid-id");

    DevClassifyOptions opts;
    opts.requireDeps = true;
    opts.freezeWorld = false;
    opts.expectedEvalPath = DevSimPathId::EvalDelayFromFreeze;

    // --- Gate: missing player ---
    {
        DevLiveWitness w;
        w.valid = true;
        w.clock = DevClockContract::get().makeStamp(5, false, false, 1.f / 240.f, 1.f / 240.f,
                                                    1.f / 240.f, 1.f, DevSimPathId::Plan);
        auto gameObject = classifyWitnessGate(w, opts);
        expect(gameObject.fidelity == DevFidelity::NotReproducible, "gate-fid");
        expect(!gameObject.claimsAllowed, "gate-claims");
        expect(gameObject.evalSuppressedReason == "missingPlayer", "gate-reason");
    }

    // Build a solid witness.
    DevLiveWitness wit;
    wit.valid = true;
    wit.playerBlobSchema = DevLiveWitness::computePlayerBlobSchema();
    wit.player.present = true;
    wit.player.x = 100.f;
    wit.player.y = 50.f;
    wit.player.yVelocity = -1.f;
    wit.clock = DevClockContract::get().makeStamp(20, true, false, 1.f / 240.f, 1.f / 480.f,
                                                  1.f / 240.f, 1.f, DevSimPathId::Plan);
    wit.worldDeps = {
        static_cast<int>(DevWitnessCategory::Player),
        static_cast<int>(DevWitnessCategory::Clock),
        static_cast<int>(DevWitnessCategory::CollisionFlags),
    };
    wit.categories.push_back({DevWitnessCategory::Player, DevWitnessStatus::Restored, 0, false});
    wit.categories.push_back({DevWitnessCategory::Clock, DevWitnessStatus::Restored, 0, false});
    wit.categories.push_back(
        {DevWitnessCategory::CollisionFlags, DevWitnessStatus::Partial, 0, false});

    auto gateOk = classifyWitnessGate(wit, opts);
    expect(gateOk.evalSuppressedReason.empty(), "gate-ok");

    // --- Reproduced: player+clock+flags restored ---
    {
        DevEvalRestore result =
            DevEvalRestore::beginFromWitness(wit, DevSimPathId::EvalDelayFromFreeze);
        DevEvalRestore::notePlayerWritten(result, wit.player);
        auto resultValue = classify(wit, result, opts);
        expect(resultValue.fidelity == DevFidelity::Reproduced, "repr-fid");
        expect(resultValue.claimsAllowed && !resultValue.approx, "repr-claims");
        expect(resultValue.evalSuppressedReason.empty(), "repr-reason");
    }

    // --- Player mismatch ---
    {
        DevEvalRestore result =
            DevEvalRestore::beginFromWitness(wit, DevSimPathId::EvalDelayFromFreeze);
        DevPlayerBlob bad = wit.player;
        bad.x = 999.f;
        DevEvalRestore::notePlayerWritten(result, bad);
        auto resultValue = classify(wit, result, opts);
        expect(resultValue.fidelity == DevFidelity::NotReproducible, "mis-fid");
        expect(!resultValue.claimsAllowed, "mis-claims");
        expect(resultValue.evalSuppressedReason == "playerMismatch", "mis-reason");
    }

    // --- Movers digest soft → playerOnlyApproximated ---
    {
        DevLiveWitness w2 = wit;
        w2.hasHazardDigest = true;
        w2.hazardDigest = 0xabc;
        w2.freezeWorld = false;
        w2.worldDeps.insert(static_cast<int>(DevWitnessCategory::Movers));
        w2.categories.push_back(
            {DevWitnessCategory::Movers, DevWitnessStatus::DigestOnly, 0xabc, false});

        DevEvalRestore result =
            DevEvalRestore::beginFromWitness(w2, DevSimPathId::EvalDelayFromFreeze);
        DevEvalRestore::notePlayerWritten(result, w2.player);
        DevEvalRestore::noteMoversDigestOnly(result, 0xabc);
        auto resultValue = classify(w2, result, opts);
        expect(resultValue.fidelity == DevFidelity::PlayerOnlyApproximated, "approx-fid");
        expect(resultValue.claimsAllowed && resultValue.approx, "approx-claims");
    }

    // --- Dual dep missing + requireDeps → notReproducible ---
    {
        DevLiveWitness w2 = wit;
        w2.dual.present = true;
        w2.dual.x = 1.f;
        w2.worldDeps.insert(static_cast<int>(DevWitnessCategory::Dual));
        DevEvalRestore result =
            DevEvalRestore::beginFromWitness(w2, DevSimPathId::EvalDelayFromFreeze);
        DevEvalRestore::notePlayerWritten(result, w2.player);
        // dual not written
        auto resultValue = classify(w2, result, opts);
        expect(resultValue.fidelity == DevFidelity::NotReproducible, "dual-hard-fid");
        expect(!resultValue.claimsAllowed, "dual-hard-claims");
        expect(resultValue.evalSuppressedReason == "requireDeps", "dual-hard-reason");
    }

    // --- Dual dep missing + requireDeps off → approx ---
    {
        DevClassifyOptions soft = opts;
        soft.requireDeps = false;
        DevLiveWitness w2 = wit;
        w2.dual.present = true;
        w2.worldDeps.insert(static_cast<int>(DevWitnessCategory::Dual));
        DevEvalRestore result =
            DevEvalRestore::beginFromWitness(w2, DevSimPathId::EvalDelayFromFreeze);
        DevEvalRestore::notePlayerWritten(result, w2.player);
        auto resultValue = classify(w2, result, soft);
        expect(resultValue.fidelity == DevFidelity::PlayerOnlyApproximated, "dual-soft-fid");
        expect(resultValue.claimsAllowed && resultValue.approx, "dual-soft-claims");
    }

    // --- Wrong eval path ---
    {
        DevEvalRestore result = DevEvalRestore::beginFromWitness(wit, DevSimPathId::Plan);
        DevEvalRestore::notePlayerWritten(result, wit.player);
        auto resultValue = classify(wit, result, opts);
        expect(resultValue.fidelity == DevFidelity::NotReproducible, "path-fid");
        expect(resultValue.evalSuppressedReason == "evalSimPath", "path-reason");
    }

    // --- Freeze-world poses + dual/orbs/triggers → reproduced ---
    {
        DevClassifyOptions freeze = opts;
        freeze.freezeWorld = true;

        DevLiveWitness w2 = wit;
        w2.freezeWorld = true;
        w2.hasHazardDigest = true;
        w2.hazardDigest = 0x111;
        DevObjectPose pose{};
        pose.uniqueId = 5;
        pose.x = 10.f;
        pose.y = 20.f;
        w2.moverPoses = {pose};
        w2.dual.present = true;
        w2.dual.x = 2.f;
        w2.orbIds = {3};
        w2.triggerCanSpool = true;
        w2.triggerStoreKey = "k";
        w2.triggerStoreVersion = 1;
        w2.triggerApplyTick = 20;
        w2.worldDeps.insert(static_cast<int>(DevWitnessCategory::Movers));
        w2.worldDeps.insert(static_cast<int>(DevWitnessCategory::Dual));
        w2.worldDeps.insert(static_cast<int>(DevWitnessCategory::Orbs));
        w2.worldDeps.insert(static_cast<int>(DevWitnessCategory::Triggers));
        w2.categories.push_back(
            {DevWitnessCategory::Movers, DevWitnessStatus::Restored, 0x111, false});
        w2.categories.push_back({DevWitnessCategory::Dual, DevWitnessStatus::Restored, 0, false});
        w2.categories.push_back({DevWitnessCategory::Orbs, DevWitnessStatus::Restored, 0, false});
        w2.categories.push_back(
            {DevWitnessCategory::Triggers, DevWitnessStatus::Partial, 0, false});

        DevEvalRestore result =
            DevEvalRestore::beginFromWitness(w2, DevSimPathId::EvalDelayFromFreeze);
        DevEvalRestore::notePlayerWritten(result, w2.player);
        DevEvalRestore::noteDualWritten(result, w2.dual);
        DevEvalRestore::noteOrbsWritten(result, w2.orbIds);
        DevEvalRestore::noteTriggersSpooled(result, w2.triggerStoreKey, w2.triggerStoreVersion,
                                            w2.triggerApplyTick);
        DevEvalRestore::noteMoversPosesWritten(result, w2.moverPoses, w2.hazardDigest);
        auto resultValue = classify(w2, result, freeze);
        expect(resultValue.fidelity == DevFidelity::Reproduced, "freeze-repr-fid");
        expect(resultValue.claimsAllowed && !resultValue.approx, "freeze-repr-claims");
        expect(resultValue.evalSuppressedReason.empty(), "freeze-repr-reason");
    }

    // --- Freeze-world without poses written → hard fail ---
    {
        DevClassifyOptions freeze = opts;
        freeze.freezeWorld = true;
        DevLiveWitness w2 = wit;
        w2.freezeWorld = true;
        w2.hasHazardDigest = true;
        w2.hazardDigest = 0x222;
        w2.worldDeps.insert(static_cast<int>(DevWitnessCategory::Movers));
        w2.categories.push_back(
            {DevWitnessCategory::Movers, DevWitnessStatus::Restored, 0x222, false});
        DevEvalRestore result =
            DevEvalRestore::beginFromWitness(w2, DevSimPathId::EvalDelayFromFreeze);
        DevEvalRestore::notePlayerWritten(result, w2.player);
        DevEvalRestore::noteMoversDigestOnly(result, 0x222);
        auto resultValue = classify(w2, result, freeze);
        expect(resultValue.fidelity == DevFidelity::NotReproducible, "freeze-hard-fid");
        expect(!resultValue.claimsAllowed, "freeze-hard-claims");
    }

    if (fails == 0) {
        report = "DevStateDiff selfCheck OK";
        return true;
    }
    report = std::string("DevStateDiff selfCheck fails: ") + report;
    return false;
}
