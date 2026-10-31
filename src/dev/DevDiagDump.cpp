#include "DevDiagDump.hpp"
#include "DevConfig.hpp"
#include "../config/StageConfig.hpp"
#include "../core/AssistClock.hpp"
#include "../core/AssistController.hpp"
#include "../core/BakeMap.hpp"
#include "../core/ConfigBundleKeys.hpp"
#include "../core/ShadowSimulator.hpp"
#include "../core/TriggerWorldStore.hpp"
#include "../debug/AssistLog.hpp"
#include "../debug/PlanGoldCorpus.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

using namespace geode::prelude;

namespace {

std::string stampNow() {
    using clock = std::chrono::system_clock;
    auto const t = clock::to_time_t(clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return std::string(buf);
}

bool writeTextFile(std::filesystem::path const& path, std::string const& body) {
    std::error_code ec;
    auto const parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
    return static_cast<bool>(f);
}

char const* boolStr(bool v) { return v ? "true" : "false"; }

std::string settingValueLine(ConfigBundleKeys::Entry const& e) {
    auto* mod = Mod::get();
    if (!mod) return fmt::format("{}=<no-mod>", e.key);
    try {
        switch (e.kind) {
            case ConfigBundleKeys::Kind::Bool:
                return fmt::format(
                    "{}={} (bool)", e.key, boolStr(mod->getSettingValue<bool>(e.key))
                );
            case ConfigBundleKeys::Kind::Int:
                return fmt::format(
                    "{}={} (int)", e.key, mod->getSettingValue<int64_t>(e.key)
                );
            case ConfigBundleKeys::Kind::Float:
                return fmt::format(
                    "{}={} (float)", e.key, mod->getSettingValue<double>(e.key)
                );
            case ConfigBundleKeys::Kind::String:
                return fmt::format(
                    "{}=\"{}\" (string)", e.key, mod->getSettingValue<std::string>(e.key)
                );
            case ConfigBundleKeys::Kind::Color: {
                auto c = mod->getSettingValue<cocos2d::ccColor3B>(e.key);
                return fmt::format(
                    "{}=#{:02X}{:02X}{:02X} (color)", e.key, c.r, c.g, c.b
                );
            }
            case ConfigBundleKeys::Kind::Keybind:
                return fmt::format("{}=<keybind> (skipped detail)", e.key);
            default:
                return fmt::format("{}=<unknown-kind>", e.key);
        }
    }
    catch (std::exception const& ex) {
        return fmt::format("{}=<err:{}>", e.key, ex.what());
    }
    catch (...) {
        return fmt::format("{}=<err>", e.key);
    }
}

} // namespace

std::filesystem::path DevDiagDump::makeStampDir(char const* prefix, std::string& errMsg) {
    errMsg.clear();
    auto* mod = Mod::get();
    if (!mod) {
        errMsg = "No Mod instance";
        return {};
    }
    auto const root = mod->getSaveDir() / "diag";
    auto const dir = root / fmt::format("{}_{}", prefix ? prefix : "diag", stampNow());
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        errMsg = fmt::format("mkdir failed: {}", ec.message());
        return {};
    }
    return dir;
}

bool DevDiagDump::writeFile(
    std::filesystem::path const& dir,
    char const* name,
    std::string const& body,
    std::string& errMsg
) {
    auto const path = dir / name;
    if (!writeTextFile(path, body)) {
        errMsg = fmt::format("write failed: {}", path.string());
        return false;
    }
    return true;
}

std::string DevDiagDump::buildIndex(std::filesystem::path const& dir, bool bakeFailOnly) {
    std::ostringstream o;
    o << "EarlyBuffer diagnostic dump\n";
    o << "dir: " << dir.string() << "\n";
    o << "mod: " << Mod::get()->getVersion().toVString() << "\n";
    o << "mode: " << (bakeFailOnly ? "bake-fail-auto" : "full-dev-dump") << "\n";
    o << "files:\n";
    o << "  00_INDEX.txt\n";
    if (!bakeFailOnly) {
        o << "  01_settings.txt\n";
    }
    o << "  02_clock_assist.txt     — simDt/TPS/startSnap vehicle flags\n";
    o << "  03_replay.txt           — raw import events\n";
    o << "  03b_replay_remap.txt    — src→bake frame map + event order\n";
    o << "  04_bakemap.txt\n";
    o << "  05_trigger_world.txt\n";
    o << "  06_bake_probe.txt       — per-click survive/death summary\n";
    o << "  06b_bake_frames.txt     — FRAME-BY-FRAME first death (+ no-TW retry)\n";
    if (!bakeFailOnly) {
        o << "  07_selfchecks.txt\n";
    }
    o << "  08_overlay.txt\n";
    o << "\nCopy this whole folder when reporting bake 0-windows bugs.\n";
    return o.str();
}

std::string DevDiagDump::buildSettings() {
    std::ostringstream o;
    o << "# Portable ConfigBundleKeys (" << ConfigBundleKeys::count() << ")\n";
    o << "# Values from live Geode setting store (Apply if drafts pending)\n\n";
    for (auto const* it = ConfigBundleKeys::begin(); it != ConfigBundleKeys::end(); ++it) {
        o << settingValueLine(*it) << "\n";
    }
    o << "\n# StageConfig / DevConfig caches\n";
    o << "enabled=" << boolStr(StageConfig::isEnabled()) << "\n";
    o << "assistMode=" << StageConfig::assistModeName(StageConfig::assistMode()) << "\n";
    o << "showTrajectory=" << boolStr(StageConfig::showTrajectory()) << "\n";
    o << "showBakePath=" << boolStr(StageConfig::showBakePath()) << "\n";
    o << "triggerBake=" << boolStr(StageConfig::triggerBake()) << "\n";
    o << "triggerBakeSkipDynamic=" << boolStr(StageConfig::triggerBakeSkipDynamic()) << "\n";
    o << "bakeRadius=" << StageConfig::bakeRadius() << "\n";
    o << "ticksAll=" << StageConfig::ticksAll() << "\n";
    o << "assistPress=" << boolStr(StageConfig::assistPress()) << "\n";
    o << "assistRelease=" << boolStr(StageConfig::assistRelease()) << "\n";
    o << "debugLogging=" << boolStr(StageConfig::debugLogging()) << "\n";
    o << "perfTrace=" << boolStr(StageConfig::perfTrace()) << "\n";
    o << "dev.recordEnable=" << boolStr(DevConfig::recordEnable()) << "\n";
    o << "dev.overlay=" << boolStr(DevConfig::overlay()) << "\n";
    return o.str();
}

static void dumpVehicleFlags(std::ostringstream& o, char const* prefix, PlayerObject* p) {
    if (!p) {
        o << prefix << "=<null>\n";
        return;
    }
    o << prefix << ".x=" << p->getPositionX() << "\n";
    o << prefix << ".y=" << p->getPositionY() << "\n";
    o << prefix << ".yVel=" << p->m_yVelocity << "\n";
    o << prefix << ".dead=" << boolStr(p->m_isDead) << "\n";
    o << prefix << ".mode=" << static_cast<int>(StageConfig::kindOf(p))
      << " (" << StageConfig::kindName(StageConfig::kindOf(p)) << ")\n";
    o << prefix << ".isDart=" << boolStr(p->m_isDart) << "\n";
    o << prefix << ".isShip=" << boolStr(p->m_isShip) << "\n";
    o << prefix << ".isBall=" << boolStr(p->m_isBall) << "\n";
    o << prefix << ".isBird=" << boolStr(p->m_isBird) << "\n";
    o << prefix << ".isRobot=" << boolStr(p->m_isRobot) << "\n";
    o << prefix << ".isSpider=" << boolStr(p->m_isSpider) << "\n";
    o << prefix << ".isSwing=" << boolStr(p->m_isSwing) << "\n";
    o << prefix << ".onGround=" << boolStr(p->m_isOnGround) << "\n";
    o << prefix << ".jumpHeld="
      << boolStr(p->m_holdingButtons[static_cast<int>(PlayerButton::Jump)]) << "\n";
}

std::string DevDiagDump::buildClockAssist() {
    std::ostringstream o;
    auto& ac = AssistController::get();
    o << "attemptTick=" << ac.attemptTick() << "\n";
    o << "lastDt=" << ac.lastDt() << "\n";
    o << "simDt=" << ac.simDt() << "\n";
    o << "estimatedTps=" << ac.estimatedTps() << "\n";
    o << "startSnapshotSlot=" << ac.startSnapshotSlot() << "\n";
    o << "AssistClock::kNominalTps=" << AssistClock::kNominalTps << "\n";
    o << "shadow.simulating=" << boolStr(ShadowSimulator::get().isSimulating()) << "\n";
    o << "shadow.bakeOffline=" << boolStr(ShadowSimulator::get().bakeOffline()) << "\n";
    o << "triggerBake=" << boolStr(StageConfig::triggerBake()) << "\n";
    dumpVehicleFlags(
        o, "startSnap",
        ShadowSimulator::get().snapshotSlot(ShadowSimulator::START_SNAPSHOT_SLOT)
    );
    auto* pl = PlayLayer::get();
    dumpVehicleFlags(o, "liveP1", pl ? pl->m_player1 : nullptr);
    dumpVehicleFlags(o, "liveP2", pl ? pl->m_player2 : nullptr);
    dumpVehicleFlags(o, "shadow", ShadowSimulator::get().shadow());
    return o.str();
}

std::string DevDiagDump::buildReplay() {
    std::ostringstream o;
    auto const* rep = BakeMap::get().pendingReplay();
    if (!rep) {
        o << "pendingReplay=none\n";
        return o.str();
    }
    o << "source=" << rep->sourceName << "\n";
    o << "format=" << rep->format << "\n";
    o << "tps=" << rep->tps << "\n";
    o << "events=" << rep->events.size() << "\n";
    int jumps = 0;
    int downs = 0;
    int firstF = -1;
    int lastF = -1;
    for (auto const& e : rep->events) {
        if (e.button != 1) continue;
        ++jumps;
        if (e.down) ++downs;
        if (firstF < 0) firstF = e.frame;
        lastF = e.frame;
    }
    int p1 = 0;
    int p2 = 0;
    for (auto const& e : rep->events) {
        if (e.player2) ++p2;
        else ++p1;
    }
    o << "jumpEvents=" << jumps << " downs=" << downs << "\n";
    o << "player1Events=" << p1 << " player2Events=" << p2 << "\n";
    o << "firstJumpFrame=" << firstF << " lastJumpFrame=" << lastF << "\n";
    o << "first20:\n";
    int n = 0;
    for (auto const& e : rep->events) {
        if (n >= 20) break;
        o << fmt::format(
            "  [{}] f={} btn={} down={} p2={}\n",
            n, e.frame, e.button, boolStr(e.down), boolStr(e.player2)
        );
        ++n;
    }
    return o.str();
}

std::string DevDiagDump::buildBakeMap() {
    std::ostringstream o;
    auto& bake = BakeMap::get();
    o << "statusLine=" << bake.statusLine() << "\n";
    o << "loaded=" << boolStr(bake.loaded()) << "\n";
    o << "baking=" << boolStr(bake.isBaking()) << "\n";
    o << "pendingReplay=" << boolStr(bake.pendingReplay() != nullptr) << "\n";
    auto const& d = bake.data();
    o << "levelKey=" << d.levelKey << "\n";
    o << "sourceReplay=" << d.sourceReplay << "\n";
    o << "method=" << d.method << "\n";
    o << "tps=" << d.tps << "\n";
    o << "clicks=" << d.clicks.size() << "\n";
    int show = static_cast<int>(std::min<size_t>(d.clicks.size(), 16));
    for (int i = 0; i < show; ++i) {
        auto const& c = d.clicks[static_cast<size_t>(i)];
        o << fmt::format(
            "  click[{}] f={} [{}..{}] x={:.1f} down={} p2={} mode={}\n",
            i, c.nominalFrame, c.earliest, c.latest, c.x,
            boolStr(c.down), boolStr(c.player2), static_cast<int>(c.mode)
        );
    }
    return o.str();
}

std::string DevDiagDump::buildTriggerWorld() {
    std::ostringstream o;
    auto& tw = TriggerWorldStore::get();
    o << "loaded=" << boolStr(tw.loaded()) << "\n";
    o << "hasTracks=" << boolStr(tw.hasTracks()) << "\n";
    o << "trackCount=" << tw.trackCount() << "\n";
    o << "readyTrackCount=" << tw.readyTrackCount() << "\n";
    o << "softcapHit=" << boolStr(tw.softcapHit()) << "\n";
    o << "tps=" << tw.data().tps << "\n";
    o << "levelKey=" << tw.data().levelKey << "\n";
    // Kind histogram
    int kinds[16]{};
    for (auto const& t : tw.data().tracks) {
        int k = static_cast<int>(t.kind);
        if (k < 0 || k >= 16) k = 15;
        ++kinds[k];
    }
    o << "kindHistogram:\n";
    for (int i = 0; i < 16; ++i) {
        if (kinds[i] <= 0) continue;
        o << fmt::format("  kind{}={}\n", i, kinds[i]);
    }
    return o.str();
}

bool DevDiagDump::prepareBakeEvents(
    PlayLayer*& outLayer,
    int& outSlot,
    float& outDt,
    double& outSrcTps,
    double& outBakeTps,
    std::vector<ReplayEvent>& outEv,
    std::string& outErr,
    int* outSubsteps
) {
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
    auto const* rep = BakeMap::get().pendingReplay();
    if (!rep || rep->events.empty()) {
        outErr = "no pending replay — Import Replay first";
        return false;
    }
    outSrcTps = rep->tps > 0.0 ? rep->tps : AssistClock::kNominalTps;
    double liveTps = AssistController::get().estimatedTps();
    // Match BakeMap::resolveBakeTps — physics-bypass stays 1:1 at author TPS.
    if (outSrcTps > AssistClock::kPlanSnapTpsHi) {
        outBakeTps = std::min(outSrcTps, 4000.0);
    }
    else if (liveTps < AssistClock::kPlanSnapTpsLo || liveTps > AssistClock::kPlanSnapTpsHi) {
        outBakeTps = AssistClock::kNominalTps;
    }
    else {
        outBakeTps = liveTps;
    }
    outDt = static_cast<float>(1.0 / outBakeTps);
    if (outSubsteps) *outSubsteps = 1;
    outEv = rep->events;
    // Match BakeMap::prepareBakeTimeline: majority-P2 → P1; else drop dual P2.
    {
        int p1 = 0;
        int p2 = 0;
        for (auto const& e : outEv) {
            if (e.player2) ++p2;
            else ++p1;
        }
        if (p2 >= p1) {
            for (auto& e : outEv) e.player2 = false;
        }
        else {
            outEv.erase(
                std::remove_if(outEv.begin(), outEv.end(), [](ReplayEvent const& e) {
                    return e.player2;
                }),
                outEv.end()
            );
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
            if (e.frame < minF) minF = e.frame;
        }
        if (minF == 1) {
            for (auto& e : outEv) e.frame -= 1;
        }
    }
    // Min 1-tick hold after remap (same-frame press+release → release+1).
    for (size_t i = 0; i < outEv.size(); ++i) {
        if (!outEv[i].down || outEv[i].button != 1) continue;
        for (size_t j = i + 1; j < outEv.size(); ++j) {
            if (outEv[j].button != outEv[i].button) continue;
            if (outEv[j].player2 != outEv[i].player2) continue;
            if (outEv[j].down) break;
            if (outEv[j].frame <= outEv[i].frame) {
                outEv[j].frame = outEv[i].frame + 1;
            }
            break;
        }
    }
    std::stable_sort(outEv.begin(), outEv.end(), [](auto const& a, auto const& b) {
        if (a.frame != b.frame) return a.frame < b.frame;
        return a.down && !b.down;
    });
    return true;
}

std::string DevDiagDump::buildReplayRemap() {
    std::ostringstream o;
    auto const* rep = BakeMap::get().pendingReplay();
    if (!rep) {
        o << "ERROR: no pending replay\n";
        return o.str();
    }
    double const srcTps = rep->tps > 0.0 ? rep->tps : AssistClock::kNominalTps;
    double liveTps = AssistController::get().estimatedTps();
    double bakeTps = srcTps;
    if (srcTps > AssistClock::kPlanSnapTpsHi) {
        bakeTps = std::min(srcTps, 4000.0);
    }
    else if (liveTps < AssistClock::kPlanSnapTpsLo || liveTps > AssistClock::kPlanSnapTpsHi) {
        bakeTps = AssistClock::kNominalTps;
    }
    else {
        bakeTps = liveTps;
    }
    o << fmt::format("srcTps={:.0f} bakeTps={:.0f}\n", srcTps, bakeTps);
    o << "columns: i srcFrame bakeFrame btn down p2(raw) → afterForceP1\n";
    float const srcDt = static_cast<float>(1.0 / srcTps);
    int const n = static_cast<int>(rep->events.size());
    int const show = std::min(n, 80);
    for (int i = 0; i < show; ++i) {
        auto const& e = rep->events[static_cast<size_t>(i)];
        int const bf = AssistClock::get().remapTick(bakeTps, e.frame, srcDt);
        o << fmt::format(
            "  [{:4}] src={:6} bake={:6} btn={} down={} p2raw={}\n",
            i, e.frame, bf, e.button, boolStr(e.down), boolStr(e.player2)
        );
    }
    if (n > show) {
        o << fmt::format("  ... ({} more events)\n", n - show);
    }
    return o.str();
}

std::string DevDiagDump::buildBakeProbe(int maxJobs) {
    std::ostringstream o;
    o << "bakeOfflinePolicy=trust-toggle-pose; hard-death-only; parkReals; applyP2→P1\n";
    o << "maxJobs=" << maxJobs << "\n";

    PlayLayer* layer = nullptr;
    int slot = -1;
    float dt = 0.f;
    double srcTps = 0.0;
    double bakeTps = 0.0;
    int substeps = 1;
    std::vector<ReplayEvent> ev;
    std::string err;
    if (!prepareBakeEvents(layer, slot, dt, srcTps, bakeTps, ev, err, &substeps)) {
        o << "ERROR: " << err << "\n";
        return o.str();
    }
    o << "startSlot=" << slot << " attemptTick=" << AssistController::get().attemptTick() << "\n";
    o << fmt::format(
        "srcTps={:.0f} bakeTps={:.0f} dt={} substeps={} events={}\n",
        srcTps, bakeTps, dt, substeps, ev.size()
    );

    bool const doRelease = StageConfig::assistRelease();
    struct Job {
        int index = 0;
        int frame = 0;
        int until = 0;
        bool down = true;
    };
    std::vector<Job> jobs;
    for (int i = 0; i < static_cast<int>(ev.size()); ++i) {
        auto const& e = ev[static_cast<size_t>(i)];
        if (e.button != 1) continue;
        if (!e.down && !doRelease) continue;
        int next = -1;
        for (int j = i + 1; j < static_cast<int>(ev.size()); ++j) {
            if (ev[static_cast<size_t>(j)].button != 1) continue;
            if (ev[static_cast<size_t>(j)].down) {
                next = ev[static_cast<size_t>(j)].frame;
                break;
            }
        }
        int horizon = ShadowSimulator::modelFor(layer->m_player1, StageConfig::ticksAll()).cap;
        int until = next > 0 ? std::min(next, e.frame + horizon) : e.frame + horizon;
        until = std::max(until, e.frame + 1);
        jobs.push_back(Job{ i, e.frame, until, e.down });
    }
    o << "jobs=" << jobs.size() << "\n\n";

    int probed = 0;
    int survived = 0;
    int died = 0;
    int unknownOnly = 0;
    for (auto const& job : jobs) {
        if (probed >= maxJobs) break;
        auto sample = ShadowSimulator::get().runBakeTimeline(
            layer, slot, ev, dt, job.frame, job.frame, true, false, true, substeps
        );
        auto full = ShadowSimulator::get().runBakeTimeline(
            layer, slot, ev, dt, job.until, -1, true, false, true, substeps
        );
        bool const ok = !full.died;
        if (ok) ++survived;
        else ++died;
        if (!full.died && full.unknown) ++unknownOnly;

        o << fmt::format(
            "job{} idx={} f={} until={} down={} | sampleX={:.1f} sampled={} | "
            "full.died={} deathF={} deathX={:.1f} deathY={:.1f} deathObjId={} "
            "deathObjType={} unknown={}\n",
            probed,
            job.index,
            job.frame,
            job.until,
            boolStr(job.down),
            sample.xAtSample,
            boolStr(sample.sampled),
            boolStr(full.died),
            full.deathFrame,
            full.deathX,
            full.deathY,
            full.deathObjId,
            full.deathObjType,
            boolStr(full.unknown)
        );
        ++probed;
    }
    o << fmt::format(
        "\nsummary probed={} survived={} died={} unknownWhileAlive={}\n",
        probed, survived, died, unknownOnly
    );
    return o.str();
}

std::string DevDiagDump::buildBakeFrameTrace() {
    std::ostringstream o;
    o << "# Frame-by-frame bake timeline (parkReals=on, P2→P1)\n";
    o << "# columns: frame x y yVel jumpHeld mode evs tw dead unk | deathObj...\n\n";

    PlayLayer* layer = nullptr;
    int slot = -1;
    float dt = 0.f;
    double srcTps = 0.0;
    double bakeTps = 0.0;
    int substeps = 1;
    std::vector<ReplayEvent> ev;
    std::string err;
    if (!prepareBakeEvents(layer, slot, dt, srcTps, bakeTps, ev, err, &substeps)) {
        o << "ERROR: " << err << "\n";
        return o.str();
    }

    // Run until first death or 128 frames — see exact sequence.
    int const until = 128;
    o << fmt::format(
        "=== TRACE A: frames 0..{} (or first death) tw=ON parkReals=ON substeps={} ===\n",
        until, substeps
    );
    auto a = ShadowSimulator::get().runBakeTimeline(
        layer, slot, ev, dt, until, -1, true, true, true, substeps
    );
    o << fmt::format(
        "result died={} deathF={} deathX={:.2f} deathY={:.2f} deathObjId={} "
        "deathObjType={} unknown={} framesLogged={}\n",
        boolStr(a.died), a.deathFrame, a.deathX, a.deathY,
        a.deathObjId, a.deathObjType, boolStr(a.unknown), a.frames.size()
    );
    for (auto const& tr : a.frames) {
        o << fmt::format(
            "  f={:4} x={:8.2f} y={:8.2f} yv={:8.3f} jmp={} mode={} evs={} tw={} "
            "dead={} unk={}",
            tr.frame, tr.x, tr.y, tr.yVel, boolStr(tr.jumpHeld), tr.mode,
            tr.eventsApplied, boolStr(tr.twApplied), boolStr(tr.dead), boolStr(tr.unknown)
        );
        if (tr.dead && tr.deathObjId >= 0) {
            o << fmt::format(
                " | killer id={} type={} @({:.1f},{:.1f})",
                tr.deathObjId, tr.deathObjType, tr.deathObjX, tr.deathObjY
            );
        }
        o << "\n";
        // Also list events that fired this frame from remapped timeline.
        for (auto const& e : ev) {
            if (e.frame != tr.frame) continue;
            o << fmt::format(
                "       +event btn={} down={}\n", e.button, boolStr(e.down)
            );
        }
    }

    // Compare: same run but without parking (legacy path).
    o << fmt::format(
        "\n=== TRACE B: frames 0..{} parkReals=OFF (legacy spawn-overlap check) ===\n",
        until
    );
    auto b = ShadowSimulator::get().runBakeTimeline(
        layer, slot, ev, dt, until, -1, true, true, false, substeps
    );
    o << fmt::format(
        "result died={} deathF={} deathX={:.2f} deathY={:.2f} deathObjId={} "
        "deathObjType={} unknown={} framesLogged={}\n",
        boolStr(b.died), b.deathFrame, b.deathX, b.deathY,
        b.deathObjId, b.deathObjType, boolStr(b.unknown), b.frames.size()
    );
    for (auto const& tr : b.frames) {
        if (!tr.dead && tr.frame > 16 && tr.frame + 1 < static_cast<int>(b.frames.size())) {
            continue; // keep B shorter: first 16 + death frame
        }
        if (tr.frame > 16 && !tr.dead) continue;
        o << fmt::format(
            "  f={:4} x={:8.2f} y={:8.2f} yv={:8.3f} jmp={} mode={} evs={} tw={} "
            "dead={} unk={}",
            tr.frame, tr.x, tr.y, tr.yVel, boolStr(tr.jumpHeld), tr.mode,
            tr.eventsApplied, boolStr(tr.twApplied), boolStr(tr.dead), boolStr(tr.unknown)
        );
        if (tr.dead && tr.deathObjId >= 0) {
            o << fmt::format(
                " | killer id={} type={} @({:.1f},{:.1f})",
                tr.deathObjId, tr.deathObjType, tr.deathObjX, tr.deathObjY
            );
        }
        o << "\n";
    }

    // If still dying at 0 with park, try first click window with full event log.
    if (!ev.empty()) {
        int firstClick = -1;
        for (auto const& e : ev) {
            if (e.button == 1 && e.down) {
                firstClick = e.frame;
                break;
            }
        }
        if (firstClick > 0) {
            int const untilClick = firstClick + 8;
            o << fmt::format(
                "\n=== TRACE C: through first click f={} (+8) parkReals=ON ===\n",
                firstClick
            );
            auto c = ShadowSimulator::get().runBakeTimeline(
                layer, slot, ev, dt, untilClick, firstClick, true, true, true, substeps
            );
            o << fmt::format(
                "result died={} deathF={} sampleX={:.2f} sampled={} deathObjId={}\n",
                boolStr(c.died), c.deathFrame, c.xAtSample, boolStr(c.sampled),
                c.deathObjId
            );
            for (auto const& tr : c.frames) {
                o << fmt::format(
                    "  f={:4} x={:8.2f} y={:8.2f} yv={:8.3f} jmp={} mode={} evs={} "
                    "tw={} dead={} unk={}\n",
                    tr.frame, tr.x, tr.y, tr.yVel, boolStr(tr.jumpHeld), tr.mode,
                    tr.eventsApplied, boolStr(tr.twApplied), boolStr(tr.dead),
                    boolStr(tr.unknown)
                );
            }
        }
    }
    return o.str();
}

std::string DevDiagDump::buildSelfChecks() {
    std::ostringstream o;
    std::string report;
    if (PlanGoldCorpus::run(report)) {
        o << "PlanGoldCorpus: OK\n" << report << "\n";
    }
    else {
        o << "PlanGoldCorpus: FAIL\n" << report << "\n";
    }
    return o.str();
}

std::string DevDiagDump::buildOverlay() {
    std::ostringstream o;
    // Force refresh then read.
    AssistController::get().updateOverlay();
    o << "attemptTick=" << AssistController::get().attemptTick() << "\n";
    o << "assistTouched=" << boolStr(AssistController::get().assistTouchedRun()) << "\n";
    o << "(see Geode console / overlay HUD for live line)\n";
    return o.str();
}

bool DevDiagDump::dumpAll(std::filesystem::path& outDir, std::string& errMsg) {
    outDir = makeStampDir("full", errMsg);
    if (outDir.empty()) return false;

    auto put = [&](char const* name, std::string const& body) -> bool {
        return writeFile(outDir, name, body, errMsg);
    };

    if (!put("00_INDEX.txt", buildIndex(outDir, false))) return false;
    if (!put("01_settings.txt", buildSettings())) return false;
    if (!put("02_clock_assist.txt", buildClockAssist())) return false;
    if (!put("03_replay.txt", buildReplay())) return false;
    if (!put("03b_replay_remap.txt", buildReplayRemap())) return false;
    if (!put("04_bakemap.txt", buildBakeMap())) return false;
    if (!put("05_trigger_world.txt", buildTriggerWorld())) return false;
    if (!put("06_bake_probe.txt", buildBakeProbe(48))) return false;
    if (!put("06b_bake_frames.txt", buildBakeFrameTrace())) return false;
    if (!put("07_selfchecks.txt", buildSelfChecks())) return false;
    if (!put("08_overlay.txt", buildOverlay())) return false;

    AssistLog::info(fmt::format("DIAG: full dump → {}", outDir.string()));
    return true;
}

bool DevDiagDump::dumpBakeFail(std::filesystem::path& outDir, std::string& errMsg) {
    outDir = makeStampDir("bakefail", errMsg);
    if (outDir.empty()) return false;

    auto put = [&](char const* name, std::string const& body) -> bool {
        return writeFile(outDir, name, body, errMsg);
    };

    if (!put("00_INDEX.txt", buildIndex(outDir, true))) return false;
    if (!put("02_clock_assist.txt", buildClockAssist())) return false;
    if (!put("03_replay.txt", buildReplay())) return false;
    if (!put("03b_replay_remap.txt", buildReplayRemap())) return false;
    if (!put("04_bakemap.txt", buildBakeMap())) return false;
    if (!put("05_trigger_world.txt", buildTriggerWorld())) return false;
    if (!put("06_bake_probe.txt", buildBakeProbe(64))) return false;
    if (!put("06b_bake_frames.txt", buildBakeFrameTrace())) return false;
    if (!put("08_overlay.txt", buildOverlay())) return false;

    AssistLog::info(fmt::format("DIAG: bake-fail dump → {}", outDir.string()));
    return true;
}
