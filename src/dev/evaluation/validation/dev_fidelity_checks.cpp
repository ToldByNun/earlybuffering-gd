#include "dev_fidelity_checks.hpp"
#include "../../capture/dev_clock_contract.hpp"
#include "../../capture/dev_live_witness.hpp"
#include "../dev_eval_restore.hpp"
#include "../dev_state_diff.hpp"
#include <cstring>
#include <fmt/format.h>
#include <string>

bool EbDevFidelityChecks::selfCheck(std::string& report) {
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

    expect(std::strcmp(EbDevStateDiff::fidelityId(DevFidelity::Reproduced), "reproduced") == 0,
           "fid-id");

    EbDevClassifyOptions opts;
    opts.requireDeps = true;
    opts.freezeWorld = false;
    opts.expectedEvalPath = DevSimPathId::EvalDelayFromFreeze;

    // --- Gate: missing player ---
    {
        EbDevLiveWitness w;
        w.valid = true;
        w.clock = EbDevClockContract::get().makeStamp(5, false, false, 1.f / 240.f, 1.f / 240.f,
                                                    1.f / 240.f, 1.f, DevSimPathId::Plan);
        auto gameObject = EbDevStateDiff::classifyWitnessGate(w, opts);
        expect(gameObject.fidelity == DevFidelity::NotReproducible, "gate-fid");
        expect(!gameObject.claimsAllowed, "gate-claims");
        expect(gameObject.evalSuppressedReason == "missingPlayer", "gate-reason");
    }

    // Build a solid witness.
    EbDevLiveWitness wit;
    wit.valid = true;
    wit.playerBlobSchema = EbDevLiveWitness::computePlayerBlobSchema();
    wit.player.present = true;
    wit.player.x = 100.f;
    wit.player.y = 50.f;
    wit.player.yVelocity = -1.f;
    wit.clock = EbDevClockContract::get().makeStamp(20, true, false, 1.f / 240.f, 1.f / 480.f,
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

    auto gateOk = EbDevStateDiff::classifyWitnessGate(wit, opts);
    expect(gateOk.evalSuppressedReason.empty(), "gate-ok");

    // --- Reproduced: player+clock+flags restored ---
    {
        EbDevEvalRestore result =
            EbDevEvalRestore::beginFromWitness(wit, DevSimPathId::EvalDelayFromFreeze);
        EbDevEvalRestore::notePlayerWritten(result, wit.player);
        auto resultValue = EbDevStateDiff::classify(wit, result, opts);
        expect(resultValue.fidelity == DevFidelity::Reproduced, "repr-fid");
        expect(resultValue.claimsAllowed && !resultValue.approx, "repr-claims");
        expect(resultValue.evalSuppressedReason.empty(), "repr-reason");
    }

    // --- Player mismatch ---
    {
        EbDevEvalRestore result =
            EbDevEvalRestore::beginFromWitness(wit, DevSimPathId::EvalDelayFromFreeze);
        EbDevPlayerBlob bad = wit.player;
        bad.x = 999.f;
        EbDevEvalRestore::notePlayerWritten(result, bad);
        auto resultValue = EbDevStateDiff::classify(wit, result, opts);
        expect(resultValue.fidelity == DevFidelity::NotReproducible, "mis-fid");
        expect(!resultValue.claimsAllowed, "mis-claims");
        expect(resultValue.evalSuppressedReason == "playerMismatch", "mis-reason");
    }

    // --- Movers digest soft → playerOnlyApproximated ---
    {
        EbDevLiveWitness w2 = wit;
        w2.hasHazardDigest = true;
        w2.hazardDigest = 0xabc;
        w2.freezeWorld = false;
        w2.worldDeps.insert(static_cast<int>(DevWitnessCategory::Movers));
        w2.categories.push_back(
            {DevWitnessCategory::Movers, DevWitnessStatus::DigestOnly, 0xabc, false});

        EbDevEvalRestore result =
            EbDevEvalRestore::beginFromWitness(w2, DevSimPathId::EvalDelayFromFreeze);
        EbDevEvalRestore::notePlayerWritten(result, w2.player);
        EbDevEvalRestore::noteMoversDigestOnly(result, 0xabc);
        auto resultValue = EbDevStateDiff::classify(w2, result, opts);
        expect(resultValue.fidelity == DevFidelity::PlayerOnlyApproximated, "approx-fid");
        expect(resultValue.claimsAllowed && resultValue.approx, "approx-claims");
    }

    // --- AP9: Dual worldDep + Absent at capture → gate missingDual ---
    {
        EbDevLiveWitness w2 = wit;
        w2.worldDeps.insert(static_cast<int>(DevWitnessCategory::Dual));
        w2.categories.push_back({DevWitnessCategory::Dual, DevWitnessStatus::Absent, 0, false});
        auto gameObject = EbDevStateDiff::classifyWitnessGate(w2, opts);
        expect(gameObject.fidelity == DevFidelity::NotReproducible, "dual-absent-gate-fid");
        expect(!gameObject.claimsAllowed, "dual-absent-gate-claims");
        expect(gameObject.evalSuppressedReason == "missingDual", "dual-absent-gate-reason");
    }

    // --- Dual dep missing on restore + requireDeps → notReproducible ---
    {
        EbDevLiveWitness w2 = wit;
        w2.dual.present = true;
        w2.dual.x = 1.f;
        w2.worldDeps.insert(static_cast<int>(DevWitnessCategory::Dual));
        w2.categories.push_back({DevWitnessCategory::Dual, DevWitnessStatus::Restored, 0, false});
        EbDevEvalRestore result =
            EbDevEvalRestore::beginFromWitness(w2, DevSimPathId::EvalDelayFromFreeze);
        EbDevEvalRestore::notePlayerWritten(result, w2.player);
        // dual not written
        auto resultValue = EbDevStateDiff::classify(w2, result, opts);
        expect(resultValue.fidelity == DevFidelity::NotReproducible, "dual-hard-fid");
        expect(!resultValue.claimsAllowed, "dual-hard-claims");
        expect(resultValue.evalSuppressedReason == "requireDeps", "dual-hard-reason");
    }

    // --- Dual dep missing on restore + requireDeps off → approx ---
    {
        EbDevClassifyOptions soft = opts;
        soft.requireDeps = false;
        EbDevLiveWitness w2 = wit;
        w2.dual.present = true;
        w2.worldDeps.insert(static_cast<int>(DevWitnessCategory::Dual));
        w2.categories.push_back({DevWitnessCategory::Dual, DevWitnessStatus::Restored, 0, false});
        EbDevEvalRestore result =
            EbDevEvalRestore::beginFromWitness(w2, DevSimPathId::EvalDelayFromFreeze);
        EbDevEvalRestore::notePlayerWritten(result, w2.player);
        auto resultValue = EbDevStateDiff::classify(w2, result, soft);
        expect(resultValue.fidelity == DevFidelity::PlayerOnlyApproximated, "dual-soft-fid");
        expect(resultValue.claimsAllowed && resultValue.approx, "dual-soft-claims");
    }

    // --- AP9: Orbs dep Partial (empty ids) → approx, claims allowed ---
    {
        EbDevLiveWitness w2 = wit;
        w2.worldDeps.insert(static_cast<int>(DevWitnessCategory::Orbs));
        w2.categories.push_back({DevWitnessCategory::Orbs, DevWitnessStatus::Partial, 0, false});
        EbDevEvalRestore result =
            EbDevEvalRestore::beginFromWitness(w2, DevSimPathId::EvalDelayFromFreeze);
        EbDevEvalRestore::notePlayerWritten(result, w2.player);
        EbDevEvalRestore::noteOrbsWritten(result, {});
        auto resultValue = EbDevStateDiff::classify(w2, result, opts);
        expect(resultValue.fidelity == DevFidelity::PlayerOnlyApproximated, "orbs-partial-fid");
        expect(resultValue.claimsAllowed && resultValue.approx, "orbs-partial-claims");
    }

    // --- AP9: contact match keeps reproduced; mismatch → approx ---
    {
        EbDevLiveWitness w2 = wit;
        w2.hasContact = true;
        w2.contact.present = true;
        w2.contact.collidedObjectId = 42;
        w2.contact.lastGroundObjectId = 7;
        EbDevEvalRestore result =
            EbDevEvalRestore::beginFromWitness(w2, DevSimPathId::EvalDelayFromFreeze);
        EbDevEvalRestore::notePlayerWritten(result, w2.player);
        EbDevEvalRestore::noteContactWritten(result, w2.contact, false);
        auto ok = EbDevStateDiff::classify(w2, result, opts);
        expect(ok.fidelity == DevFidelity::Reproduced, "contact-match-fid");
        expect(ok.claimsAllowed && !ok.approx, "contact-match-claims");

        EbDevContactSnap bad = w2.contact;
        bad.collidedObjectId = 99;
        EbDevEvalRestore badRestore =
            EbDevEvalRestore::beginFromWitness(w2, DevSimPathId::EvalDelayFromFreeze);
        EbDevEvalRestore::notePlayerWritten(badRestore, w2.player);
        EbDevEvalRestore::noteContactWritten(badRestore, bad, false);
        auto badResult = EbDevStateDiff::classify(w2, badRestore, opts);
        expect(badResult.fidelity == DevFidelity::PlayerOnlyApproximated, "contact-mis-fid");
        expect(badResult.claimsAllowed && badResult.approx, "contact-mis-claims");
    }

    // --- Wrong eval path ---
    {
        EbDevEvalRestore result = EbDevEvalRestore::beginFromWitness(wit, DevSimPathId::Plan);
        EbDevEvalRestore::notePlayerWritten(result, wit.player);
        auto resultValue = EbDevStateDiff::classify(wit, result, opts);
        expect(resultValue.fidelity == DevFidelity::NotReproducible, "path-fid");
        expect(resultValue.evalSuppressedReason == "evalSimPath", "path-reason");
    }

    // --- Freeze-world poses + dual/orbs/triggers → reproduced ---
    {
        EbDevClassifyOptions freeze = opts;
        freeze.freezeWorld = true;

        EbDevLiveWitness w2 = wit;
        w2.freezeWorld = true;
        w2.hasHazardDigest = true;
        w2.hazardDigest = 0x111;
        EbDevObjectPose pose{};
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

        EbDevEvalRestore result =
            EbDevEvalRestore::beginFromWitness(w2, DevSimPathId::EvalDelayFromFreeze);
        EbDevEvalRestore::notePlayerWritten(result, w2.player);
        EbDevEvalRestore::noteDualWritten(result, w2.dual);
        EbDevEvalRestore::noteOrbsWritten(result, w2.orbIds);
        EbDevEvalRestore::noteTriggersSpooled(result, w2.triggerStoreKey, w2.triggerStoreVersion,
                                            w2.triggerApplyTick);
        EbDevEvalRestore::noteMoversPosesWritten(result, w2.moverPoses, w2.hazardDigest);
        auto resultValue = EbDevStateDiff::classify(w2, result, freeze);
        expect(resultValue.fidelity == DevFidelity::Reproduced, "freeze-repr-fid");
        expect(resultValue.claimsAllowed && !resultValue.approx, "freeze-repr-claims");
        expect(resultValue.evalSuppressedReason.empty(), "freeze-repr-reason");
    }

    // --- Freeze-world without poses written → hard fail ---
    {
        EbDevClassifyOptions freeze = opts;
        freeze.freezeWorld = true;
        EbDevLiveWitness w2 = wit;
        w2.freezeWorld = true;
        w2.hasHazardDigest = true;
        w2.hazardDigest = 0x222;
        w2.worldDeps.insert(static_cast<int>(DevWitnessCategory::Movers));
        w2.categories.push_back(
            {DevWitnessCategory::Movers, DevWitnessStatus::Restored, 0x222, false});
        EbDevEvalRestore result =
            EbDevEvalRestore::beginFromWitness(w2, DevSimPathId::EvalDelayFromFreeze);
        EbDevEvalRestore::notePlayerWritten(result, w2.player);
        EbDevEvalRestore::noteMoversDigestOnly(result, 0x222);
        auto resultValue = EbDevStateDiff::classify(w2, result, freeze);
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
