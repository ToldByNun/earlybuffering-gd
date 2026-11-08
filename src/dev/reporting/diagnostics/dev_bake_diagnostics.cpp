#include "dev_bake_diagnostics.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../core/assist/assist_clock.hpp"
#include "../../../core/assist/assist_controller.hpp"
#include "../../../core/bake/bake_map.hpp"
#include "../../../core/replay/replay_import.hpp"
#include "../../../core/sim/shadow_simulator.hpp"
#include "../../../core/trigger_world/trigger_world_store.hpp"
#include "../dev_diag_dump.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

bool DevBakeDiagnostics::prepareBakeEvents(PlayLayer*& outLayer, int& outSlot, float& outDt,
                                           double& outSrcTps, double& outBakeTps,
                                           std::vector<ReplayEvent>& outEv, std::string& outErr,
                                           int* outSubsteps, DevDiagDump::BakePrepStats* outStats) {
    outLayer = PlayLayer::get();
    if (!outLayer) {
        outErr = "no PlayLayer — enter a level, reset, pause, then dump";
        return false;
    }
    auto snapRes = AssistController::get().ensureStartSnapshot(outLayer);
    if (snapRes.isErr()) {
        outErr = snapRes.unwrapErr();
        return false;
    }
    outSlot = AssistController::get().startSnapshotSlot();
    auto const* report = BakeMap::get().pendingReplay();
    if (!report || report->events.empty()) {
        outErr = "no pending replay — Import Replay first";
        return false;
    }
    outSrcTps = report->tps > 0.0 ? report->tps : AssistClock::kNominalTps;
    double liveTps = AssistController::get().estimatedTps();
    // Match BakeMap::resolveBakeTps — physics-bypass stays 1:1 at author TPS.
    if (outSrcTps > AssistClock::kPlanSnapTpsHi) {
        outBakeTps = std::min(outSrcTps, 4000.0);
    } else if (liveTps < AssistClock::kPlanSnapTpsLo || liveTps > AssistClock::kPlanSnapTpsHi) {
        outBakeTps = AssistClock::kNominalTps;
    } else {
        outBakeTps = liveTps;
    }
    outDt = static_cast<float>(1.0 / outBakeTps);
    if (outSubsteps)
        *outSubsteps = 1;
    outEv = report->events;
    DevDiagDump::BakePrepStats stats{};
    // Match BakeMap::prepareBakeTimeline: majority-P2 → P1; else drop dual P2.
    {
        int playerOne = 0;
        int playerTwo = 0;
        for (auto const& e : outEv) {
            if (e.player2)
                ++playerTwo;
            else
                ++playerOne;
        }
        if (playerTwo >= playerOne) {
            stats.forcedP2toP1 = playerTwo > 0;
            for (auto& e : outEv)
                e.player2 = false;
        } else {
            stats.droppedP2 = playerTwo;
            outEv.erase(std::remove_if(outEv.begin(), outEv.end(),
                                       [](ReplayEvent const& e) { return e.player2; }),
                        outEv.end());
        }
    }
    if (std::fabs(outSrcTps - outBakeTps) >= 0.5) {
        float const srcDt = static_cast<float>(1.0 / outSrcTps);
        for (auto& e : outEv) {
            e.frame = AssistClock::get().remapTick(outBakeTps, e.frame, srcDt);
        }
    }
    // Match BakeMap: 1-based GDR → 0-based sim.
    {
        int minF = std::numeric_limits<int>::max();
        for (auto const& e : outEv) {
            if (e.frame < minF)
                minF = e.frame;
        }
        stats.minFrameBeforeShift = (minF == std::numeric_limits<int>::max()) ? -1 : minF;
        if (minF == 1) {
            stats.shiftedMinus1 = true;
            for (auto& e : outEv)
                e.frame -= 1;
        }
    }
    // Min 1-tick hold after remap (same-frame press+release → release+1).
    for (size_t index = 0; index < outEv.size(); ++index) {
        if (!outEv[index].down || outEv[index].button != 1)
            continue;
        for (size_t otherIndex = index + 1; otherIndex < outEv.size(); ++otherIndex) {
            if (outEv[otherIndex].button != outEv[index].button)
                continue;
            if (outEv[otherIndex].player2 != outEv[index].player2)
                continue;
            if (outEv[otherIndex].down)
                break;
            if (outEv[otherIndex].frame <= outEv[index].frame) {
                outEv[otherIndex].frame = outEv[index].frame + 1;
            }
            break;
        }
    }
    std::stable_sort(outEv.begin(), outEv.end(), [](auto const& a, auto const& b) {
        if (a.frame != b.frame)
            return a.frame < b.frame;
        return a.down && !b.down;
    });
    if (outStats)
        *outStats = stats;
    return true;
}

std::string DevBakeDiagnostics::buildReplayRemap() {
    std::ostringstream o;
    PlayLayer* layer = nullptr;
    int slot = -1;
    float deltaTime = 0.f;
    double srcTps = 0.0;
    double bakeTps = 0.0;
    std::vector<ReplayEvent> ev;
    std::string err;
    DevDiagDump::BakePrepStats stats{};
    if (!DevDiagDump::prepareBakeEvents(layer, slot, deltaTime, srcTps, bakeTps, ev, err, nullptr,
                                        &stats)) {
        o << "ERROR: " << err << "\n";
        return o.str();
    }
    auto const* report = BakeMap::get().pendingReplay();
    o << "# Commit 3: frames AFTER prepareBakeTimeline (TPS remap + P2 policy + optional −1)\n";
    o << fmt::format("srcTps={:.0f} bakeTps={:.0f} shiftedMinus1={} minFrameBeforeShift={} "
                     "droppedP2={} forcedP2toP1={} postEvents={}\n",
                     srcTps, bakeTps, DevDiagDumpImplementation::boolStr(stats.shiftedMinus1),
                     stats.minFrameBeforeShift, stats.droppedP2,
                     DevDiagDumpImplementation::boolStr(stats.forcedP2toP1), ev.size());
    o << "columns: i srcFrame(raw) postBakeFrame btn down p2raw kept\n";

    // Pair raw src → post timeline by index when sizes match after P2 drop is hard;
    // show post list primarily (this is what the sim runs).
    int const nPost = static_cast<int>(ev.size());
    int const show = std::min(nPost, 120);
    for (int index = 0; index < show; ++index) {
        auto const& e = ev[static_cast<size_t>(index)];
        int srcFrame = -1;
        if (report && index < static_cast<int>(report->events.size())) {
            // Best-effort: same index only when we forced P2→P1 (size preserved).
            if (stats.forcedP2toP1 || stats.droppedP2 == 0) {
                srcFrame = report->events[static_cast<size_t>(index)].frame;
            }
        }
        o << fmt::format("  [{:4}] src={:6} post={:6} btn={} down={} p2={}\n", index, srcFrame,
                         e.frame, e.button, DevDiagDumpImplementation::boolStr(e.down),
                         DevDiagDumpImplementation::boolStr(e.player2));
    }
    if (nPost > show) {
        o << fmt::format("  ... ({} more post-prep events)\n", nPost - show);
    }
    // Around first death if probe finds one (post frames near death).
    int lastEv = 0;
    for (auto const& e : ev) {
        if (e.frame > lastEv)
            lastEv = e.frame;
    }
    auto probe = ShadowSimulator::get().runBakeTimeline(layer, slot, ev, deltaTime,
                                                        DevDiagDump::deathTraceUntil(-1, lastEv),
                                                        -1, true, false, true, 1);
    if (probe.died && probe.deathFrame >= 0) {
        o << fmt::format("\n# events with postBakeFrame in [death-8, death+8] deathF={}\n",
                         probe.deathFrame);
        int const lowerBound = std::max(0, probe.deathFrame - 8);
        int const upperBound = probe.deathFrame + 8;
        for (int index = 0; index < nPost; ++index) {
            auto const& e = ev[static_cast<size_t>(index)];
            if (e.frame < lowerBound || e.frame > upperBound)
                continue;
            o << fmt::format("  [{:4}] post={:6} btn={} down={}\n", index, e.frame, e.button,
                             DevDiagDumpImplementation::boolStr(e.down));
        }
    }
    return o.str();
}

std::string DevBakeDiagnostics::buildBakeProbe(int maxJobs) {
    std::ostringstream o;
    o << "bakeOfflinePolicy=Soft-portals-Include; hard-death-only; parkReals; "
         "P2→P1; parityGate=eps-locked; tg2-residual=TW-empty|TW-pose\n";
    o << "maxJobs=" << maxJobs << "\n";

    PlayLayer* layer = nullptr;
    int slot = -1;
    float deltaTime = 0.f;
    double srcTps = 0.0;
    double bakeTps = 0.0;
    int substeps = 1;
    std::vector<ReplayEvent> ev;
    std::string err;
    if (!DevDiagDump::prepareBakeEvents(layer, slot, deltaTime, srcTps, bakeTps, ev, err,
                                        &substeps)) {
        o << "ERROR: " << err << "\n";
        return o.str();
    }
    o << "startSlot=" << slot << " attemptTick=" << AssistController::get().attemptTick() << "\n";
    o << fmt::format("srcTps={:.0f} bakeTps={:.0f} dt={} substeps={} events={}\n", srcTps, bakeTps,
                     deltaTime, substeps, ev.size());

    bool const doRelease = StageConfig::assistRelease();
    class Job {
    public:
        int index = 0;
        int frame = 0;
        int until = 0;
        bool down = true;

    private:
    };
    std::vector<Job> jobs;
    for (int index = 0; index < static_cast<int>(ev.size()); ++index) {
        auto const& e = ev[static_cast<size_t>(index)];
        if (e.button != 1)
            continue;
        if (!e.down && !doRelease)
            continue;
        int next = -1;
        for (int otherIndex = index + 1; otherIndex < static_cast<int>(ev.size()); ++otherIndex) {
            if (ev[static_cast<size_t>(otherIndex)].button != 1)
                continue;
            if (ev[static_cast<size_t>(otherIndex)].down) {
                next = ev[static_cast<size_t>(otherIndex)].frame;
                break;
            }
        }
        int horizon = ShadowSimulator::modelFor(layer->m_player1, StageConfig::ticksAll()).cap;
        int until = next > 0 ? std::min(next, e.frame + horizon) : e.frame + horizon;
        until = std::max(until, e.frame + 1);
        jobs.push_back(Job{index, e.frame, until, e.down});
    }
    o << "jobs=" << jobs.size() << "\n\n";

    int probed = 0;
    int survived = 0;
    int died = 0;
    int unknownOnly = 0;
    for (auto const& job : jobs) {
        if (probed >= maxJobs)
            break;
        auto sample = ShadowSimulator::get().runBakeTimeline(
            layer, slot, ev, deltaTime, job.frame, job.frame, true, false, true, substeps);
        auto full = ShadowSimulator::get().runBakeTimeline(layer, slot, ev, deltaTime, job.until,
                                                           -1, true, false, true, substeps);
        bool const ok = !full.died;
        if (ok)
            ++survived;
        else
            ++died;
        if (!full.died && full.unknown)
            ++unknownOnly;

        o << fmt::format("job{} idx={} f={} until={} down={} | sampleX={:.1f} sampled={} | "
                         "full.died={} deathF={} deathX={:.1f} deathY={:.1f} deathObjId={} "
                         "deathObjType={} unknown={}\n",
                         probed, job.index, job.frame, job.until,
                         DevDiagDumpImplementation::boolStr(job.down), sample.xAtSample,
                         DevDiagDumpImplementation::boolStr(sample.sampled),
                         DevDiagDumpImplementation::boolStr(full.died), full.deathFrame,
                         full.deathX, full.deathY, full.deathObjId, full.deathObjType,
                         DevDiagDumpImplementation::boolStr(full.unknown));
        ++probed;
    }
    o << fmt::format("\nsummary probed={} survived={} died={} unknownWhileAlive={}\n", probed,
                     survived, died, unknownOnly);
    return o.str();
}

std::string DevBakeDiagnostics::buildBakeFrameTrace() {
    std::ostringstream o;
    o << "# Frame-by-frame bake timeline (parkReals=on) — Commit 3 death+8\n";
    o << "# columns: frame x y yVel jumpHeld mode evs tw dead unk | deathObj...\n\n";

    PlayLayer* layer = nullptr;
    int slot = -1;
    float deltaTime = 0.f;
    double srcTps = 0.0;
    double bakeTps = 0.0;
    int substeps = 1;
    std::vector<ReplayEvent> ev;
    std::string err;
    DevDiagDump::BakePrepStats stats{};
    if (!DevDiagDump::prepareBakeEvents(layer, slot, deltaTime, srcTps, bakeTps, ev, err, &substeps,
                                        &stats)) {
        o << "ERROR: " << err << "\n";
        return o.str();
    }
    int lastEv = 0;
    for (auto const& e : ev) {
        if (e.frame > lastEv)
            lastEv = e.frame;
    }
    o << fmt::format("prep shiftedMinus1={} droppedP2={} forcedP2toP1={} lastEv={} twTracks={}\n",
                     DevDiagDumpImplementation::boolStr(stats.shiftedMinus1), stats.droppedP2,
                     DevDiagDumpImplementation::boolStr(stats.forcedP2toP1), lastEv,
                     TriggerWorldStore::get().trackCount());

    // Probe without frame log to find death, then re-run through death+8 with frames.
    int const probeUntil = DevDiagDump::deathTraceUntil(-1, lastEv);
    auto probe = ShadowSimulator::get().runBakeTimeline(layer, slot, ev, deltaTime, probeUntil, -1,
                                                        true, false, true, substeps);
    int const until = DevDiagDump::deathTraceUntil(probe.died ? probe.deathFrame : -1,
                                                   probe.died ? -1 : std::min(lastEv, 56));
    o << fmt::format(
        "=== TRACE A: frames 0..{} (deathF={} → death+8) parkReals=ON substeps={} ===\n", until,
        probe.deathFrame, substeps);
    auto firstValue = ShadowSimulator::get().runBakeTimeline(layer, slot, ev, deltaTime, until, -1,
                                                             true, true, true, substeps);
    o << fmt::format(
        "result died={} deathF={} deathX={:.2f} deathY={:.2f} deathObjId={} "
        "deathObjType={} deathObjGameId={} unknown={} softHits={} hardHits={} "
        "unknownTop3=[{}] framesLogged={}\n",
        DevDiagDumpImplementation::boolStr(firstValue.died), firstValue.deathFrame,
        firstValue.deathX, firstValue.deathY, firstValue.deathObjId, firstValue.deathObjType,
        firstValue.deathObjGameId, DevDiagDumpImplementation::boolStr(firstValue.unknown),
        firstValue.unknownSoftHits, firstValue.unknownHardHits,
        DevDiagDump::formatUnknownTop3(firstValue.unknownHist), firstValue.frames.size());
    for (auto const& tr : firstValue.frames) {
        o << fmt::format("  f={:4} x={:8.2f} y={:8.2f} yv={:8.3f} jmp={} mode={} evs={} tw={} "
                         "dead={} unk={}",
                         tr.frame, tr.x, tr.y, tr.yVel,
                         DevDiagDumpImplementation::boolStr(tr.jumpHeld), tr.mode, tr.eventsApplied,
                         DevDiagDumpImplementation::boolStr(tr.twApplied),
                         DevDiagDumpImplementation::boolStr(tr.dead),
                         DevDiagDumpImplementation::boolStr(tr.unknown));
        if (tr.dead && tr.deathObjId >= 0) {
            o << fmt::format(" | killer id={} type={} @({:.1f},{:.1f})", tr.deathObjId,
                             tr.deathObjType, tr.deathObjX, tr.deathObjY);
        }
        o << "\n";
        for (auto const& e : ev) {
            if (e.frame != tr.frame)
                continue;
            o << fmt::format("       +event btn={} down={}\n", e.button,
                             DevDiagDumpImplementation::boolStr(e.down));
        }
    }

    // Compare: same until without parking (legacy path) — short log.
    o << fmt::format("\n=== TRACE B: frames 0..{} parkReals=OFF (legacy spawn-overlap check) ===\n",
                     until);
    auto secondValue = ShadowSimulator::get().runBakeTimeline(layer, slot, ev, deltaTime, until, -1,
                                                              true, true, false, substeps);
    o << fmt::format(
        "result died={} deathF={} deathX={:.2f} deathY={:.2f} deathObjId={} "
        "deathObjType={} unknown={} framesLogged={}\n",
        DevDiagDumpImplementation::boolStr(secondValue.died), secondValue.deathFrame,
        secondValue.deathX, secondValue.deathY, secondValue.deathObjId, secondValue.deathObjType,
        DevDiagDumpImplementation::boolStr(secondValue.unknown), secondValue.frames.size());
    for (auto const& tr : secondValue.frames) {
        bool const nearDeath = secondValue.died && tr.frame + 8 >= secondValue.deathFrame;
        if (!tr.dead && tr.frame > 16 && !nearDeath)
            continue;
        o << fmt::format("  f={:4} x={:8.2f} y={:8.2f} yv={:8.3f} jmp={} mode={} evs={} tw={} "
                         "dead={} unk={}",
                         tr.frame, tr.x, tr.y, tr.yVel,
                         DevDiagDumpImplementation::boolStr(tr.jumpHeld), tr.mode, tr.eventsApplied,
                         DevDiagDumpImplementation::boolStr(tr.twApplied),
                         DevDiagDumpImplementation::boolStr(tr.dead),
                         DevDiagDumpImplementation::boolStr(tr.unknown));
        if (tr.dead && tr.deathObjId >= 0) {
            o << fmt::format(" | killer id={} type={} @({:.1f},{:.1f})", tr.deathObjId,
                             tr.deathObjType, tr.deathObjX, tr.deathObjY);
        }
        o << "\n";
    }

    if (!ev.empty()) {
        int firstClick = -1;
        for (auto const& e : ev) {
            if (e.button == 1 && e.down) {
                firstClick = e.frame;
                break;
            }
        }
        if (firstClick >= 0) {
            int const untilClick = firstClick + 8;
            o << fmt::format("\n=== TRACE C: through first click f={} (+8) parkReals=ON ===\n",
                             firstClick);
            auto candidate = ShadowSimulator::get().runBakeTimeline(
                layer, slot, ev, deltaTime, untilClick, firstClick, true, true, true, substeps);
            o << fmt::format("result died={} deathF={} sampleX={:.2f} sampled={} deathObjId={}\n",
                             DevDiagDumpImplementation::boolStr(candidate.died),
                             candidate.deathFrame, candidate.xAtSample,
                             DevDiagDumpImplementation::boolStr(candidate.sampled),
                             candidate.deathObjId);
            for (auto const& tr : candidate.frames) {
                o << fmt::format("  f={:4} x={:8.2f} y={:8.2f} yv={:8.3f} jmp={} mode={} evs={} "
                                 "tw={} dead={} unk={}\n",
                                 tr.frame, tr.x, tr.y, tr.yVel,
                                 DevDiagDumpImplementation::boolStr(tr.jumpHeld), tr.mode,
                                 tr.eventsApplied, DevDiagDumpImplementation::boolStr(tr.twApplied),
                                 DevDiagDumpImplementation::boolStr(tr.dead),
                                 DevDiagDumpImplementation::boolStr(tr.unknown));
            }
        }
    }
    return o.str();
}

std::string DevBakeDiagnostics::buildUnknownHistogram() {
    std::ostringstream o;
    o << "# Soft/Hard Unknown overlap histogram (Commit 3)\n";
    o << "# Soft = Include-candidate later; Hard = toggle/dynamic/dual honesty truncate\n\n";

    PlayLayer* layer = nullptr;
    int slot = -1;
    float deltaTime = 0.f;
    double srcTps = 0.0;
    double bakeTps = 0.0;
    int substeps = 1;
    std::vector<ReplayEvent> ev;
    std::string err;
    DevDiagDump::BakePrepStats stats{};
    if (!DevDiagDump::prepareBakeEvents(layer, slot, deltaTime, srcTps, bakeTps, ev, err, &substeps,
                                        &stats)) {
        o << "ERROR: " << err << "\n";
        return o.str();
    }
    int lastEv = 0;
    for (auto const& e : ev) {
        if (e.frame > lastEv)
            lastEv = e.frame;
    }
    auto probe = ShadowSimulator::get().runBakeTimeline(layer, slot, ev, deltaTime,
                                                        DevDiagDump::deathTraceUntil(-1, lastEv),
                                                        -1, true, false, true, substeps);
    int const until = DevDiagDump::deathTraceUntil(probe.died ? probe.deathFrame : -1, lastEv);
    auto run = ShadowSimulator::get().runBakeTimeline(layer, slot, ev, deltaTime, until, -1, true,
                                                      false, true, substeps);
    o << fmt::format("until={} died={} deathF={} deathObjId={} deathObjGameId={} "
                     "twTracks={} droppedP2={} shiftedMinus1={}\n",
                     until, DevDiagDumpImplementation::boolStr(run.died), run.deathFrame,
                     run.deathObjId, run.deathObjGameId, TriggerWorldStore::get().trackCount(),
                     stats.droppedP2, DevDiagDumpImplementation::boolStr(stats.shiftedMinus1));
    o << fmt::format("softHits={} hardHits={} bins={} unknownTop3=[{}]\n\n", run.unknownSoftHits,
                     run.unknownHardHits, run.unknownHist.size(),
                     DevDiagDump::formatUnknownTop3(run.unknownHist));
    o << "columns: rank class objectId objectType uniqueId count\n";
    int rank = 0;
    for (auto const& b : run.unknownHist) {
        o << fmt::format("  [{:2}] {} oid={} type={} uid={} count={}\n", rank,
                         b.hard ? "hard" : "soft", b.objectId, b.objectType, b.uniqueId, b.count);
        ++rank;
        if (rank >= 64) {
            o << fmt::format("  ... ({} more bins)\n", run.unknownHist.size() - 64);
            break;
        }
    }
    if (run.unknownHist.empty()) {
        o << "  (no UnknownOnOverlap hits in this timeline)\n";
    }
    return o.str();
}
