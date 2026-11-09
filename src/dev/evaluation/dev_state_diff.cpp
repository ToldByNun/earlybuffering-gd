#include "dev_state_diff.hpp"
#include "../capture/dev_clock_contract.hpp"
#include "../capture/dev_live_witness.hpp"
#include "dev_eval_restore.hpp"
#include "validation/dev_fidelity_checks.hpp"
#include <cmath>
#include <fmt/format.h>
#include <string>
#include <vector>

char const* EbDevStateDiff::fidelityId(DevFidelity frame) {
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

bool EbDevStateDiff::parseFidelity(std::string const& s, DevFidelity& out) {
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

char const* EbDevStateDiff::diffKindId(DevDiffKind offset) {
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

void EbDevStateDiff::pushDiff(std::vector<EbDevDiffEntry>& diffs, DevWitnessCategory category,
                            char const* field, std::string liveValue, std::string restoredValue,
                            DevDiffKind kind) {
    EbDevDiffEntry e;
    e.category = category;
    e.fieldOrObjectId = field ? field : "";
    e.liveValue = std::move(liveValue);
    e.restoredValue = std::move(restoredValue);
    e.kind = kind;
    diffs.push_back(std::move(e));
}

bool EbDevStateDiff::isHardFailDep(DevWitnessCategory candidate, EbDevClassifyOptions const& opts) {
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

bool EbDevStateDiff::playerBlobsEqual(EbDevPlayerBlob const& live, EbDevPlayerBlob const& restored,
                                    DevWitnessCategory category,
                                    std::vector<EbDevDiffEntry>* outDiffs) {
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

EbDevClassifyResult EbDevStateDiff::classifyWitnessGate(EbDevLiveWitness const& witness,
                                                    EbDevClassifyOptions const& opts) {
    (void)opts;
    EbDevClassifyResult out;
    out.fidelity = DevFidelity::NotReproducible;
    out.claimsAllowed = false;
    out.approx = false;

    if (!witness.valid) {
        out.evalSuppressedReason = "witnessInvalid";
        return out;
    }
    if (!witness.player.present ||
        EbDevLiveWitness::categoryStatus(witness, DevWitnessCategory::Player) ==
            DevWitnessStatus::Absent) {
        out.evalSuppressedReason = "missingPlayer";
        pushDiff(out.diffs, DevWitnessCategory::Player, "blob", "absent", "",
                 DevDiffKind::Unrestored);
        return out;
    }
    uint64_t const expectSchema = EbDevLiveWitness::computePlayerBlobSchema();
    if (witness.playerBlobSchema != 0 && witness.playerBlobSchema != expectSchema) {
        out.evalSuppressedReason = "playerSchema";
        pushDiff(out.diffs, DevWitnessCategory::Player, "playerBlobSchema",
                 fmt::format("{:016x}", witness.playerBlobSchema),
                 fmt::format("{:016x}", expectSchema), DevDiffKind::Mismatch);
        return out;
    }
    if (!witness.clock.valid || !EbDevClockContract::get().clockOk(witness.clock) ||
        EbDevLiveWitness::categoryStatus(witness, DevWitnessCategory::Clock) ==
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

EbDevClassifyResult EbDevStateDiff::classify(EbDevLiveWitness const& witness,
                                         EbDevEvalRestore const& restore,
                                         EbDevClassifyOptions const& opts) {
    EbDevClassifyResult out;

    // Witness-level hard fails first.
    EbDevClassifyResult gate = classifyWitnessGate(witness, opts);
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
    if (!restore.clock.valid || !EbDevClockContract::get().clockOk(restore.clock) ||
        EbDevEvalRestore::categoryStatus(restore, DevWitnessCategory::Clock) !=
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
                 EbDevClockContract::simPathId(opts.expectedEvalPath),
                 EbDevClockContract::simPathId(restore.evalSimPathId), DevDiffKind::Mismatch);
        return out;
    }

    // Player must be restored and match.
    if (EbDevEvalRestore::categoryStatus(restore, DevWitnessCategory::Player) !=
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

        DevWitnessStatus const state = EbDevEvalRestore::categoryStatus(restore, dep);

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
                         EbDevLiveWitness::statusId(EbDevLiveWitness::categoryStatus(witness, dep)),
                         EbDevLiveWitness::statusId(state),
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
                pushDiff(out.diffs, dep, "blob", "witness", EbDevLiveWitness::statusId(state),
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
                         EbDevLiveWitness::statusId(state), DevDiffKind::Unrestored);
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
                         EbDevLiveWitness::statusId(state), DevDiffKind::Unrestored);
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
            pushDiff(out.diffs, dep, "status", "restored", EbDevLiveWitness::statusId(state),
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

bool EbDevStateDiff::selfCheck(std::string& report) {
    return EbDevFidelityChecks::selfCheck(report);
}
