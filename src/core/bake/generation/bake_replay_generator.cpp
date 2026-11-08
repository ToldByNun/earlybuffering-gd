#include "bake_replay_generator.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../../dev/reporting/dev_diag_dump.hpp"
#include "../../assist/assist_clock.hpp"
#include "../../assist/assist_controller.hpp"
#include "../../assist/contracts/assist_contracts140.hpp"
#include "../../fixtures/tg2_bake_fixture.hpp"
#include "../../learning/level_profile.hpp"
#include "../../replay/replay_import.hpp"
#include "../../sim/shadow_simulator.hpp"
#include "../../trigger_world/trigger_world_baker.hpp"
#include "../../trigger_world/trigger_world_store.hpp"
#include "../bake_map.hpp"
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <limits>
#include <string>
#include <vector>

geode::Result<int> BakeReplayGenerator::bakeReplay(BakeMap& owner, PlayLayer* layer,
                                                   int startSnapshotSlot, int bakeRadius) {
    if (!layer)
        return geode::Err("No PlayLayer");
    if (!owner.m_hasReplay || owner.m_replay.events.empty())
        return geode::Err("No replay loaded");
    if (startSnapshotSlot < 0)
        return geode::Err("No start snapshot (reset once, then bake)");

    bakeRadius = BakeMapImplementation::resolveRadius(bakeRadius, BakeMethod::Replay);

    // World must match start snapshot: bake right after reset, not mid-run.
    // Mid-level orb claims / toggles make the ghost die immediately at start.
    int const attemptTick = AssistController::get().attemptTick();
    if (attemptTick > 60) {
        AssistLog::info(fmt::format("BAKE warn: attemptTick={} — prefer reset→pause→bake (mid-run "
                                    "world pollutes orbs/toggles)",
                                    attemptTick));
    }
    // Commit 4: never validate while TW is still building / stale-empty.
    if (StageConfig::triggerBake()) {
        owner.m_progress.setStatus("waiting Trigger World…");
        std::string twErr;
        if (!TriggerWorldBaker::get().waitUntilReady(layer, 45000, twErr)) {
            return geode::Err(twErr.empty() ? "Trigger World not ready for bake" : twErr);
        }
        auto& tw = TriggerWorldStore::get();
        AssistLog::info(fmt::format("BAKE: TW ready tracks={} ready={} softcap={} ({})",
                                    tw.trackCount(), tw.readyTrackCount(), tw.softcapHit() ? 1 : 0,
                                    TriggerWorldBaker::get().statusLine()));
        if (tw.softcapHit()) {
            AssistLog::info(fmt::format(
                "BAKE warn: Trigger World softcap ({} tracks) — movers may be incomplete",
                tw.trackCount()));
        }
    }

    // Mild FPS (60/360) → resample onto live ~240. Physics-bypass (1000–3000)
    // stays 1:1 at author TPS so the ghost matches the bot quantum exactly.
    double const srcTps = owner.m_replay.tps > 0.0 ? owner.m_replay.tps : AssistClock::kNominalTps;
    double const liveTps = AssistController::get().estimatedTps();
    double const tps = BakeMapImplementation::resolveBakeTps(srcTps, liveTps);
    float const deltaTime = static_cast<float>(1.0 / tps);
    bakeRadius = BakeMapImplementation::scaleBakeTicks(bakeRadius, tps);

    std::vector<ReplayEvent> event =
        BakeMapImplementation::prepareBakeTimeline(owner.m_replay.events, srcTps, tps, true);
    AssistLog::info(fmt::format("BAKE replay: timeline {} events @ {:.0f}←{:.0f} tps radius={} "
                                "(validate 1:1 then expand windows)",
                                event.size(), tps, srcTps, bakeRadius));

    BakeMapData out;
    out.levelKey = owner.m_data.levelKey.empty() && layer->m_level
                       ? LevelProfile::makeKey(layer->m_level)
                       : owner.m_data.levelKey;
    if (out.levelKey.empty())
        out.levelKey = "unknown";
    out.sourceReplay = owner.m_replay.sourceName;
    out.tps = tps;
    out.method = "replay";

    bool const doRelease = StageConfig::assistRelease();

    std::vector<ReplayJumpJob> jobs;
    for (int index = 0; index < static_cast<int>(event.size()); ++index) {
        auto const& e = event[static_cast<size_t>(index)];
        if (e.button != 1)
            continue;
        if (!e.down && !doRelease)
            continue;
        int F = e.frame;
        int next = BakeMapImplementation::nextJumpFrame(event, index, e.player2);
        int horizon = ShadowSimulator::modelFor(layer->m_player1, StageConfig::ticksAll()).cap;
        if (horizon <= 0)
            horizon = std::max(16, StageConfig::ticksAll() * 4);
        horizon = BakeMapImplementation::scaleBakeTicks(horizon, tps);
        int until = next > 0 ? std::min(next, F + horizon) : F + horizon;
        until = std::max(until, F + 1);
        jobs.push_back(ReplayJumpJob{index, F, e.down, e.player2, until});
    }
    if (jobs.empty()) {
        return geode::Err("Replay has no Jump events to bake (import a macro with clicks)");
    }

    int lastFrame = 0;
    for (auto const& e : event) {
        if (e.frame > lastFrame)
            lastFrame = e.frame;
    }
    // Progress: validate frames + per-click expand.
    int const validateWeight = std::max(1, lastFrame);
    owner.m_progress.total = validateWeight + static_cast<int>(jobs.size());
    owner.m_progress.done = 0;
    owner.m_progress.setStatus(
        fmt::format("validate macro 1:1 · {}f @ {:.0f} tps", lastFrame, tps));
    AssistLog::info(
        fmt::format("BAKE replay: validate 0..{} then expand {} clicks, radius {} @ {:.0f} tps",
                    lastFrame, jobs.size(), bakeRadius, tps));

    // Commit 6 Parity Gate: Soft portals Include + freeze ε before validate
    // (portal Include must not retune noise).
    {
        std::string portalDetail;
        if (!Tg2BakeFixture::bakeOfflinePortalsIncludeOk(portalDetail)) {
            return geode::Err(
                fmt::format("Bake portal policy failed ({}) — Soft portals must Include",
                            portalDetail.empty() ? "?" : portalDetail));
        }
        std::string parityDetail;
        AssistContracts140::applyLockedEps();
        if (!Tg2BakeFixture::parityGateOk(parityDetail)) {
            return geode::Err(
                fmt::format("Bake parity gate failed ({}) — ε LOCK required before validate",
                            parityDetail.empty() ? "?" : parityDetail));
        }
        AssistLog::debug("BAKE: parity gate OK (eps locked; Soft portals Include)");
    }

    // Start pose (StartPos / normal). Snapshot X tells Hybrid parts where bake begins.
    bool const startPosAttempt = layer->m_isTestMode;
    float startX = 0.f;
    if (auto* snap = ShadowSimulator::get().snapshotSlot(startSnapshotSlot)) {
        startX = snap->getPositionX();
    }
    AssistLog::info(fmt::format(
        "BAKE: startPose startPos={} startX={:.1f} attemptTick={} twTracks={}",
        startPosAttempt ? 1 : 0, startX, attemptTick, TriggerWorldStore::get().trackCount()));

    // ── Validate 1:1 from start snapshot. Death ≠ hard fail: keep the
    // surviving prefix (Hybrid often only needs a StartPos part, not the
    // whole macro). Only frame-0 (no prefix) still errors.
    ShadowSimulator::BakeRunResult validate;
    {
        std::lock_guard lock(owner.m_simMutex);
        int lastLogged = -1;
        validate = ShadowSimulator::get().runBakeTimeline(
            layer, startSnapshotSlot, event, deltaTime, lastFrame, -1, true, false, true, 1,
            [&](int frame) -> bool {
                if (owner.m_progress.cancel.load())
                    return false;
                if (frame - lastLogged >= 64 || frame == lastFrame) {
                    lastLogged = frame;
                    owner.m_progress.done = frame;
                    owner.m_progress.setStatus(fmt::format("validate {}/{}", frame, lastFrame));
                }
                return true;
            });
    }
    if (owner.m_progress.cancel.load())
        return geode::Err("Bake cancelled");

    int partialCut = -1; // frames >= cut are dropped; -1 = full macro OK
    if (validate.died) {
        // P2 drop stats for err / log (same policy as prepareBakeTimeline).
        int droppedP2 = 0;
        {
            int playerOne = 0;
            int playerTwo = 0;
            for (auto const& e : owner.m_replay.events) {
                if (e.player2)
                    ++playerTwo;
                else
                    ++playerOne;
            }
            if (playerTwo < playerOne)
                droppedP2 = playerTwo;
        }
        auto const top3 = DevDiagDump::formatUnknownTop3(validate.unknownHist);
        int const twTracks = TriggerWorldStore::get().trackCount();
        auto const residual = Tg2BakeFixture::formatResidual(
            twTracks, validate.deathFrame, validate.deathObjId, validate.deathObjType);
        AssistLog::info(
            fmt::format("BAKE: macro dies in sim @ frame {} deathX={:.1f} deathY={:.1f} "
                        "objId={} objType={} gameId={} twTracks={} droppedP2={} "
                        "unknownTop3=[{}] soft={} hard={} residual={} startPos={} startX={:.1f}",
                        validate.deathFrame, validate.deathX, validate.deathY, validate.deathObjId,
                        validate.deathObjType, validate.deathObjGameId, twTracks, droppedP2, top3,
                        validate.unknownSoftHits, validate.unknownHardHits,
                        residual.empty() ? "-" : residual, startPosAttempt ? 1 : 0, startX));
        {
            std::filesystem::path diagDir;
            std::string diagErr;
            if (DevDiagDump::dumpBakeFail(diagDir, diagErr)) {
                AssistLog::info(
                    fmt::format("BAKE: validate death — wrote diag {}", diagDir.string()));
            }
        }

        int const cut = validate.deathFrame;
        if (cut <= 0) {
            // No surviving prefix — cannot expand any click from this pose.
            std::string errorMessage =
                fmt::format("Macro dies in sim at frame {} (x={:.0f}) deathObjId={} "
                            "twTracks={} unknownTop3=[{}] droppedP2={} startPos={} startX={:.0f}",
                            validate.deathFrame, validate.deathX, validate.deathObjId, twTracks,
                            top3, droppedP2, startPosAttempt ? 1 : 0, startX);
            if (!residual.empty()) {
                errorMessage += " — ";
                errorMessage += residual;
            }
            if (startPosAttempt) {
                errorMessage += " — StartPos bake: reset on the StartPos, pause, bake; "
                                "macro frames must be from the same StartPos attempt; "
                                "TW empty often kills frame 0 (wait Trigger World).";
            } else {
                errorMessage += " — ghost path ≠ bot. Reset→pause→Bake.";
            }
            errorMessage += " Diag: /diag/bakefail_* parityGate=eps-locked";
            return geode::Err(errorMessage);
        }

        // Partial prefix: keep clicks strictly before death; Hybrid parts OK.
        partialCut = cut;
        int const before = static_cast<int>(jobs.size());
        jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
                                  [cut](ReplayJumpJob const& j) { return j.frame >= cut; }),
                   jobs.end());
        for (auto& j : jobs) {
            if (j.until > cut)
                j.until = cut;
        }
        AssistLog::info(
            fmt::format("BAKE: partial prefix — died @{} keeping {}/{} clicks before cut "
                        "(Hybrid parts; not whole-macro)",
                        cut, jobs.size(), before));
        if (jobs.empty()) {
            return geode::Err(fmt::format("Macro dies at frame {} before any bake click "
                                          "(x={:.0f}, startPos={}, startX={:.0f}, twTracks={}). "
                                          "Diag: /diag/bakefail_*",
                                          cut, validate.deathX, startPosAttempt ? 1 : 0, startX,
                                          twTracks));
        }
    }

    owner.m_progress.done = validateWeight;
    owner.m_progress.setStatus(
        fmt::format("expand windows · {} clicks · radius {}", jobs.size(), bakeRadius));
    if (partialCut >= 0) {
        AssistLog::info(fmt::format("BAKE: expanding {} click windows (partial cut @{})",
                                    jobs.size(), partialCut));
    } else {
        AssistLog::info(
            fmt::format("BAKE: validate OK through frame {} — expanding {} click windows",
                        lastFrame, jobs.size()));
    }

    // ── Phase 2: binary-search earliest/latest around each click ───────
    for (auto const& job : jobs) {
        if (owner.m_progress.cancel.load())
            return geode::Err("Bake cancelled");

        std::lock_guard lock(owner.m_simMutex);
        auto sample = ShadowSimulator::get().runBakeTimeline(layer, startSnapshotSlot, event,
                                                             deltaTime, job.frame, job.frame);
        float x = sample.sampled ? sample.xAtSample : 0.f;
        auto mode = sample.sampled ? sample.modeAtSample : GameModeKind::Cube;

        // Nominal already survived full validate — still re-check shift window.
        if (!owner.testShiftedClick(layer, startSnapshotSlot, event, job.eventIndex, job.frame,
                                    job.until, deltaTime)) {
            AssistLog::debug(fmt::format("BAKE: skip click @{} (shift re-check died)", job.frame));
            owner.m_progress.done.fetch_add(1);
            continue;
        }

        int lowerBound = std::max(0, job.frame - bakeRadius);
        int upperBound = job.frame;
        while (lowerBound < upperBound) {
            int mid = lowerBound + (upperBound - lowerBound) / 2;
            if (owner.testShiftedClick(layer, startSnapshotSlot, event, job.eventIndex, mid,
                                       job.until, deltaTime)) {
                upperBound = mid;
            } else {
                lowerBound = mid + 1;
            }
        }
        int earliest = lowerBound;

        lowerBound = job.frame;
        upperBound = job.frame + bakeRadius;
        while (lowerBound < upperBound) {
            int mid = lowerBound + (upperBound - lowerBound + 1) / 2;
            if (owner.testShiftedClick(layer, startSnapshotSlot, event, job.eventIndex, mid,
                                       job.until, deltaTime)) {
                lowerBound = mid;
            } else {
                upperBound = mid - 1;
            }
        }
        int latest = lowerBound;

        BakedClick c;
        c.nominalFrame = job.frame;
        c.earliest = earliest;
        c.latest = latest;
        c.down = job.down;
        c.player2 = job.player2;
        c.x = x;
        c.mode = mode;
        // Release while dashing → dash/hold-orb; purple viz + later release gate.
        c.holdAssist = !job.down && sample.sampled && sample.dashingAtSample;
        out.clicks.push_back(c);

        owner.m_progress.done.fetch_add(1);
        owner.m_progress.setStatus(fmt::format("expand {}/{} · @{} → [{},{}]",
                                               static_cast<int>(out.clicks.size()), jobs.size(),
                                               job.frame, earliest, latest));
    }

    int const nClicks = static_cast<int>(out.clicks.size());
    AssistLog::info(fmt::format("BAKE done clicks={} jobs={} lastFrame={} tps={:.0f} source={} "
                                "partialCut={} startPos={} (replay)",
                                nClicks, jobs.size(), lastFrame, tps, out.sourceReplay, partialCut,
                                startPosAttempt ? 1 : 0));

    if (nClicks <= 0) {
        {
            std::filesystem::path diagDir;
            std::string diagErr;
            if (DevDiagDump::dumpBakeFail(diagDir, diagErr)) {
                AssistLog::info(
                    fmt::format("BAKE: 0 windows after validate — diag {}", diagDir.string()));
            }
        }
        return geode::Err("Replay bake produced 0 windows — expand failed. "
                          "Diag: /diag/bakefail_*");
    }

    if (partialCut >= 0) {
        // Surface partial in progress status (scheduler success alert stays click count).
        owner.m_progress.setStatus(
            fmt::format("partial {} windows (cut @{})", nClicks, partialCut));
    }

    owner.m_data = std::move(out);
    owner.m_loaded = true;
    owner.m_dirty = true;
    owner.save();
    return geode::Ok(nClicks);
}
