#include "dev_diag_dump.hpp"
#include "../../config/stage_config.hpp"
#include "../../core/assist/assist_clock.hpp"
#include "../../core/assist/assist_controller.hpp"
#include "../../core/bake/bake_map.hpp"
#include "../../core/bundle/config_bundle_keys.hpp"
#include "../../core/level/section_object_walk.hpp"
#include "../../core/sim/shadow_simulator.hpp"
#include "../../core/trigger_world/trigger_world_baker.hpp"
#include "../../core/trigger_world/trigger_world_store.hpp"
#include "../../debug/assist_log.hpp"
#include "../../debug/plan_gold_corpus.hpp"
#include "../dev_config.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

std::string DevDiagDumpImplementation::stampNow() {
    using clock = std::chrono::system_clock;
    auto const timestamp = clock::to_time_t(clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &timestamp);
#else
    localtime_r(&timestamp, &tm);
#endif
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &tm);
    return std::string(buffer);
}

bool DevDiagDumpImplementation::writeTextFile(std::filesystem::path const& path,
                                              std::string const& body) {
    std::error_code ec;
    auto const parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    std::ofstream outputFile(path, std::ios::binary | std::ios::trunc);
    if (!outputFile)
        return false;
    outputFile.write(body.data(), static_cast<std::streamsize>(body.size()));
    return static_cast<bool>(outputFile);
}

char const* DevDiagDumpImplementation::boolStr(bool value) {
    return value ? "true" : "false";
}

std::string DevDiagDumpImplementation::settingValueLine(ConfigBundleKeys::Entry const& e) {
    auto* mod = geode::prelude::Mod::get();
    if (!mod)
        return fmt::format("{}=<no-mod>", e.key);
    try {
        switch (e.kind) {
        case ConfigBundleKeys::Kind::Bool:
            return fmt::format(
                "{}={} (bool)", e.key,
                DevDiagDumpImplementation::boolStr(mod->getSettingValue<bool>(e.key)));
        case ConfigBundleKeys::Kind::Int:
            return fmt::format("{}={} (int)", e.key, mod->getSettingValue<int64_t>(e.key));
        case ConfigBundleKeys::Kind::Float:
            return fmt::format("{}={} (float)", e.key, mod->getSettingValue<double>(e.key));
        case ConfigBundleKeys::Kind::String:
            return fmt::format("{}=\"{}\" (string)", e.key,
                               mod->getSettingValue<std::string>(e.key));
        case ConfigBundleKeys::Kind::Color: {
            auto candidate = mod->getSettingValue<cocos2d::ccColor3B>(e.key);
            return fmt::format("{}=#{:02X}{:02X}{:02X} (color)", e.key, candidate.r, candidate.g,
                               candidate.b);
        }
        case ConfigBundleKeys::Kind::Keybind:
            return fmt::format("{}=<keybind> (skipped detail)", e.key);
        default:
            return fmt::format("{}=<unknown-kind>", e.key);
        }
    } catch (std::exception const& ex) {
        return fmt::format("{}=<err:{}>", e.key, ex.what());
    } catch (...) {
        return fmt::format("{}=<err>", e.key);
    }
}

std::filesystem::path DevDiagDump::makeStampDir(char const* prefix, std::string& errMsg) {
    errMsg.clear();
    auto* mod = geode::prelude::Mod::get();
    if (!mod) {
        errMsg = "No Mod instance";
        return {};
    }
    auto const root = mod->getSaveDir() / "diag";
    auto const directory = root / fmt::format("{}_{}", prefix ? prefix : "diag",
                                              DevDiagDumpImplementation::stampNow());
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        errMsg = fmt::format("mkdir failed: {}", ec.message());
        return {};
    }
    return directory;
}

bool DevDiagDump::writeFile(std::filesystem::path const& dir, char const* name,
                            std::string const& body, std::string& errMsg) {
    auto const path = dir / name;
    if (!DevDiagDumpImplementation::writeTextFile(path, body)) {
        errMsg = fmt::format("write failed: {}", path.string());
        return false;
    }
    return true;
}

std::string DevDiagDump::buildIndex(std::filesystem::path const& dir, bool bakeFailOnly) {
    std::ostringstream o;
    o << "EarlyBuffer diagnostic dump\n";
    o << "dir: " << dir.string() << "\n";
    o << "mod: " << geode::prelude::Mod::get()->getVersion().toVString() << "\n";
    o << "mode: " << (bakeFailOnly ? "bake-fail-auto" : "full-dev-dump") << "\n";
    o << "files:\n";
    o << "  00_INDEX.txt\n";
    if (!bakeFailOnly) {
        o << "  01_settings.txt\n";
    }
    o << "  02_clock_assist.txt     — simDt/TPS/startSnap vehicle flags\n";
    o << "  02b_ghost_hitbox.txt    — P1 vs shadow rect + hazard overlaps\n";
    o << "  03_replay.txt           — raw import events\n";
    o << "  03b_replay_remap.txt    — AFTER prepareBakeTimeline (incl −1)\n";
    o << "  04_bakemap.txt\n";
    o << "  05_trigger_world.txt\n";
    o << "  06_bake_probe.txt       — per-click survive/death summary\n";
    o << "  06b_bake_frames.txt     — FRAME-BY-FRAME through death+8\n";
    o << "  06c_unknown_hist.txt    — Soft/Hard Unknown objectId histogram\n";
    if (!bakeFailOnly) {
        o << "  07_selfchecks.txt\n";
    }
    o << "  08_overlay.txt\n";
    o << "\nCopy this whole folder when reporting bake 0-windows bugs.\n";
    return o.str();
}

int DevDiagDump::deathTraceUntil(int deathFrame, int lastEventFrame) {
    // Commit 3: always cover death+8; never hard-cap at 128.
    int until = 0;
    if (deathFrame >= 0) {
        until = deathFrame + 8;
    }
    if (lastEventFrame >= 0) {
        until = std::max(until, lastEventFrame + 8);
    }
    if (until < 8)
        until = 8;
    // Safety: enormous macros still get a bound (death+8 path is short).
    if (deathFrame < 0 && until > 4096)
        until = 4096;
    return until;
}

std::string
DevDiagDump::formatUnknownTop3(std::vector<ShadowSimulator::UnknownHistBin> const& hist) {
    if (hist.empty())
        return "-";
    std::string s;
    int countValue = 0;
    for (auto const& b : hist) {
        if (countValue >= 3)
            break;
        if (!s.empty())
            s += ",";
        s += fmt::format("{}:{}:{}x", b.hard ? "H" : "S", b.objectId, b.count);
        ++countValue;
    }
    return s.empty() ? "-" : s;
}

bool DevDiagDump::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += "; ";
        report += name;
    };
    expect(deathTraceUntil(185, 100) == 193, "death185-plus8");
    expect(deathTraceUntil(50, 200) == 208, "lastEv-wins");
    expect(deathTraceUntil(-1, 10) == 18, "no-death-lastEv");
    expect(deathTraceUntil(-1, -1) == 8, "floor8");
    // Surviving huge macro without death is capped.
    expect(deathTraceUntil(-1, 10000) == 4096, "survive-cap");

    std::vector<ShadowSimulator::UnknownHistBin> empty;
    expect(formatUnknownTop3(empty) == "-", "top3-empty");
    std::vector<ShadowSimulator::UnknownHistBin> bins = {
        {10, 0, 1, true, 5},
        {20, 0, 2, false, 3},
        {30, 0, 3, false, 1},
        {40, 0, 4, true, 1},
    };
    expect(formatUnknownTop3(bins) == "H:10:5x,S:20:3x,S:30:1x", "top3-fmt");

    if (fails == 0) {
        report = "DevDiagDump selfCheck OK";
        return true;
    }
    report = fmt::format("DevDiagDump selfCheck {} fails: {}", fails, report);
    return false;
}

std::string DevDiagDump::buildSettings() {
    std::ostringstream o;
    o << "# Portable ConfigBundleKeys (" << ConfigBundleKeys::count() << ")\n";
    o << "# Values from live Geode setting store (Apply if drafts pending)\n\n";
    for (auto const* it = ConfigBundleKeys::begin(); it != ConfigBundleKeys::end(); ++it) {
        o << DevDiagDumpImplementation::settingValueLine(*it) << "\n";
    }
    o << "\n# StageConfig / DevConfig caches\n";
    o << "enabled=" << DevDiagDumpImplementation::boolStr(StageConfig::isEnabled()) << "\n";
    o << "assistMode=" << StageConfig::assistModeName(StageConfig::assistMode()) << "\n";
    o << "showTrajectory=" << DevDiagDumpImplementation::boolStr(StageConfig::showTrajectory())
      << "\n";
    o << "showBakePath=" << DevDiagDumpImplementation::boolStr(StageConfig::showBakePath()) << "\n";
    o << "triggerBake=" << DevDiagDumpImplementation::boolStr(StageConfig::triggerBake()) << "\n";
    o << "triggerBakeSkipDynamic="
      << DevDiagDumpImplementation::boolStr(StageConfig::triggerBakeSkipDynamic()) << "\n";
    o << "bakeRadius=" << StageConfig::bakeRadius() << "\n";
    o << "ticksAll=" << StageConfig::ticksAll() << "\n";
    o << "assistPress=" << DevDiagDumpImplementation::boolStr(StageConfig::assistPress()) << "\n";
    o << "assistRelease=" << DevDiagDumpImplementation::boolStr(StageConfig::assistRelease())
      << "\n";
    o << "debugLogging=" << DevDiagDumpImplementation::boolStr(StageConfig::debugLogging()) << "\n";
    o << "perfTrace=" << DevDiagDumpImplementation::boolStr(StageConfig::perfTrace()) << "\n";
    o << "dev.recordEnable=" << DevDiagDumpImplementation::boolStr(DevConfig::recordEnable())
      << "\n";
    o << "dev.overlay=" << DevDiagDumpImplementation::boolStr(DevConfig::overlay()) << "\n";
    return o.str();
}

void DevDiagDumpImplementation::dumpVehicleFlags(std::ostringstream& o, char const* prefix,
                                                 PlayerObject* player) {
    if (!player) {
        o << prefix << "=<null>\n";
        return;
    }
    o << prefix << ".x=" << player->getPositionX() << "\n";
    o << prefix << ".y=" << player->getPositionY() << "\n";
    o << prefix << ".yVel=" << player->m_yVelocity << "\n";
    o << prefix << ".dead=" << DevDiagDumpImplementation::boolStr(player->m_isDead) << "\n";
    o << prefix << ".mode=" << static_cast<int>(StageConfig::kindOf(player)) << " ("
      << StageConfig::kindName(StageConfig::kindOf(player)) << ")\n";
    o << prefix << ".isDart=" << DevDiagDumpImplementation::boolStr(player->m_isDart) << "\n";
    o << prefix << ".isShip=" << DevDiagDumpImplementation::boolStr(player->m_isShip) << "\n";
    o << prefix << ".isBall=" << DevDiagDumpImplementation::boolStr(player->m_isBall) << "\n";
    o << prefix << ".isBird=" << DevDiagDumpImplementation::boolStr(player->m_isBird) << "\n";
    o << prefix << ".isRobot=" << DevDiagDumpImplementation::boolStr(player->m_isRobot) << "\n";
    o << prefix << ".isSpider=" << DevDiagDumpImplementation::boolStr(player->m_isSpider) << "\n";
    o << prefix << ".isSwing=" << DevDiagDumpImplementation::boolStr(player->m_isSwing) << "\n";
    o << prefix << ".onGround=" << DevDiagDumpImplementation::boolStr(player->m_isOnGround) << "\n";
    o << prefix << ".jumpHeld="
      << DevDiagDumpImplementation::boolStr(
             player->m_holdingButtons[static_cast<int>(PlayerButton::Jump)])
      << "\n";
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
    o << "shadow.simulating="
      << DevDiagDumpImplementation::boolStr(ShadowSimulator::get().isSimulating()) << "\n";
    o << "shadow.bakeOffline="
      << DevDiagDumpImplementation::boolStr(ShadowSimulator::get().bakeOffline()) << "\n";
    o << "triggerBake=" << DevDiagDumpImplementation::boolStr(StageConfig::triggerBake()) << "\n";
    DevDiagDumpImplementation::dumpVehicleFlags(
        o, "startSnap", ShadowSimulator::get().snapshotSlot(ShadowSimulator::START_SNAPSHOT_SLOT));
    auto* playLayer = PlayLayer::get();
    DevDiagDumpImplementation::dumpVehicleFlags(o, "liveP1",
                                                playLayer ? playLayer->m_player1 : nullptr);
    DevDiagDumpImplementation::dumpVehicleFlags(o, "liveP2",
                                                playLayer ? playLayer->m_player2 : nullptr);
    DevDiagDumpImplementation::dumpVehicleFlags(o, "shadow", ShadowSimulator::get().shadow());
    return o.str();
}

bool DevDiagDumpImplementation::isHazardType(GameObject* object) {
    if (!object)
        return false;
    auto const tick = object->m_objectType;
    return tick == GameObjectType::Hazard || tick == GameObjectType::AnimatedHazard;
}

void DevDiagDumpImplementation::dumpRectLine(std::ostringstream& o, char const* name,
                                             PlayerObject* player) {
    if (!player) {
        o << name << "=<null>\n";
        return;
    }
    auto const result = player->getObjectRect();
    o << fmt::format("{}: pos=({:.2f},{:.2f}) rot={:.2f} scale=({:.3f},{:.3f}) vehicleSize={:.3f} "
                     "content=({:.2f},{:.2f}) rect=({:.2f},{:.2f},{:.2f},{:.2f}) area={:.1f} "
                     "jumpHeld={} isDart={}\n",
                     name, player->getPositionX(), player->getPositionY(), player->getRotation(),
                     player->getScaleX(), player->getScaleY(), player->m_vehicleSize,
                     player->getContentSize().width, player->getContentSize().height,
                     result.origin.x, result.origin.y, result.size.width, result.size.height,
                     result.size.width * result.size.height,
                     DevDiagDumpImplementation::boolStr(
                         player->m_holdingButtons[static_cast<int>(PlayerButton::Jump)]),
                     DevDiagDumpImplementation::boolStr(player->m_isDart));
}

int DevDiagDumpImplementation::countHazardOverlaps(PlayLayer* layer, PlayerObject* player,
                                                   std::ostringstream& detail, int maxDetail) {
    if (!layer || !player)
        return 0;
    auto const pref = player->getObjectRect();
    int countValue = 0;
    auto consider = [&](GameObject* object) {
        if (!DevDiagDumpImplementation::isHazardType(object))
            return;
        auto const hr = object->getObjectRect();
        if (!pref.intersectsRect(hr))
            return;
        ++countValue;
        if (countValue <= maxDetail) {
            detail << fmt::format(
                "  hazard id={} type={} @({:.1f},{:.1f}) rect=({:.1f},{:.1f},{:.1f},{:.1f})\n",
                object->m_uniqueID, static_cast<int>(object->m_objectType), object->getPositionX(),
                object->getPositionY(), hr.origin.x, hr.origin.y, hr.size.width, hr.size.height);
        }
    };
    if (auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr) {
        for (unsigned index = 0; index < children->count(); ++index) {
            // Same live-pointer filter as sections — children can be stale too.
            consider(SectionObjectWalk::asGameObject(children->objectAtIndex(index)));
        }
    }
    // Never raw typeinfo_cast on m_sections — poison slots AV (bakefail diag).
    SectionObjectWalk::forEachInSections(layer, consider);
    return countValue;
}

std::string DevDiagDump::buildGhostHitbox() {
    std::ostringstream o;
    o << "# Ghost vs live hitbox after copyPlayerState(startSnap) — BEFORE physics\n";
    o << "# If shadow rect >> P1 or exclusive hazard overlaps → false frame-0 deaths\n\n";

    auto* layer = PlayLayer::get();
    if (!layer || !layer->m_player1) {
        o << "ERROR: no PlayLayer / P1\n";
        return o.str();
    }
    auto& sim = ShadowSimulator::get();
    auto* snap = sim.snapshotSlot(ShadowSimulator::START_SNAPSHOT_SLOT);
    auto* shadow = sim.shadow();
    if (!snap || !shadow) {
        o << "ERROR: missing start snap or shadow — reset once in level\n";
        return o.str();
    }

    // Ensure start snap exists for this attempt.
    (void)AssistController::get().ensureStartSnapshot(layer);
    snap = sim.snapshotSlot(ShadowSimulator::START_SNAPSHOT_SLOT);

    DevDiagDumpImplementation::dumpRectLine(o, "liveP1(before)", layer->m_player1);
    DevDiagDumpImplementation::dumpRectLine(o, "startSnap", snap);
    DevDiagDumpImplementation::dumpRectLine(o, "shadow(beforeCopy)", shadow);

    ShadowSimulator::copyPlayerState(shadow, snap);
    DevDiagDumpImplementation::dumpRectLine(o, "shadow(afterCopy)", shadow);

    auto const p1r = layer->m_player1->getObjectRect();
    auto const shr = shadow->getObjectRect();
    o << fmt::format("rectDelta: dw={:.2f} dh={:.2f} dArea={:.1f}\n",
                     shr.size.width - p1r.size.width, shr.size.height - p1r.size.height,
                     (shr.size.width * shr.size.height) - (p1r.size.width * p1r.size.height));

    std::ostringstream p1Haz;
    std::ostringstream shHaz;
    int const p1Hits =
        DevDiagDumpImplementation::countHazardOverlaps(layer, layer->m_player1, p1Haz, 12);
    int const shHits = DevDiagDumpImplementation::countHazardOverlaps(layer, shadow, shHaz, 12);
    o << fmt::format("hazardOverlaps liveP1={} shadow={}\n", p1Hits, shHits);
    if (p1Hits > 0) {
        o << "liveP1 overlaps:\n" << p1Haz.str();
    }
    if (shHits > 0) {
        o << "shadow overlaps:\n" << shHaz.str();
    }
    o << fmt::format("exclusiveShadowHazards={}\n",
                     shHits > p1Hits ? "LIKELY (ghost hitbox/world mismatch)" : "none-or-equal");

    // TW capture size + bake gdStep for current pending replay dt.
    o << fmt::format("twCapturedTransforms={}\n", sim.diagCaptureTransformCount(layer));

    float deltaTime = AssistController::get().simDt();
    if (!(deltaTime > 0.f))
        deltaTime = AssistClock::kNominalSimDt;
    auto const* report = BakeMap::get().pendingReplay();
    if (report && report->tps > AssistClock::kPlanSnapTpsHi) {
        deltaTime = static_cast<float>(1.0 / std::min(report->tps, 4000.0));
    }
    o << fmt::format("probeDt={} gdStep={}\n", deltaTime, AssistClock::secondsToGdStep(deltaTime));
    return o.str();
}

std::string DevDiagDump::buildReplay() {
    std::ostringstream o;
    auto const* report = BakeMap::get().pendingReplay();
    if (!report) {
        o << "pendingReplay=none\n";
        return o.str();
    }
    o << "source=" << report->sourceName << "\n";
    o << "format=" << report->format << "\n";
    o << "tps=" << report->tps << "\n";
    o << "events=" << report->events.size() << "\n";
    int jumps = 0;
    int downs = 0;
    int firstF = -1;
    int lastF = -1;
    for (auto const& e : report->events) {
        if (e.button != 1)
            continue;
        ++jumps;
        if (e.down)
            ++downs;
        if (firstF < 0)
            firstF = e.frame;
        lastF = e.frame;
    }
    int playerOne = 0;
    int playerTwo = 0;
    for (auto const& e : report->events) {
        if (e.player2)
            ++playerTwo;
        else
            ++playerOne;
    }
    o << "jumpEvents=" << jumps << " downs=" << downs << "\n";
    o << "player1Events=" << playerOne << " player2Events=" << playerTwo << "\n";
    o << "firstJumpFrame=" << firstF << " lastJumpFrame=" << lastF << "\n";
    o << "first20:\n";
    int count = 0;
    for (auto const& e : report->events) {
        if (count >= 20)
            break;
        o << fmt::format("  [{}] f={} btn={} down={} p2={}\n", count, e.frame, e.button,
                         DevDiagDumpImplementation::boolStr(e.down),
                         DevDiagDumpImplementation::boolStr(e.player2));
        ++count;
    }
    return o.str();
}

std::string DevDiagDump::buildBakeMap() {
    std::ostringstream o;
    auto& bake = BakeMap::get();
    o << "statusLine=" << bake.statusLine() << "\n";
    o << "loaded=" << DevDiagDumpImplementation::boolStr(bake.loaded()) << "\n";
    o << "baking=" << DevDiagDumpImplementation::boolStr(bake.isBaking()) << "\n";
    o << "pendingReplay=" << DevDiagDumpImplementation::boolStr(bake.pendingReplay() != nullptr)
      << "\n";
    auto const& d = bake.data();
    o << "levelKey=" << d.levelKey << "\n";
    o << "sourceReplay=" << d.sourceReplay << "\n";
    o << "method=" << d.method << "\n";
    o << "tps=" << d.tps << "\n";
    o << "clicks=" << d.clicks.size() << "\n";
    int show = static_cast<int>(std::min<size_t>(d.clicks.size(), 16));
    for (int index = 0; index < show; ++index) {
        auto const& c = d.clicks[static_cast<size_t>(index)];
        o << fmt::format("  click[{}] f={} [{}..{}] x={:.1f} down={} p2={} mode={}\n", index,
                         c.nominalFrame, c.earliest, c.latest, c.x,
                         DevDiagDumpImplementation::boolStr(c.down),
                         DevDiagDumpImplementation::boolStr(c.player2), static_cast<int>(c.mode));
    }
    return o.str();
}

std::string DevDiagDump::buildTriggerWorld() {
    std::ostringstream o;
    auto& tw = TriggerWorldStore::get();
    auto& baker = TriggerWorldBaker::get();
    o << "loaded=" << DevDiagDumpImplementation::boolStr(tw.loaded()) << "\n";
    o << "hasTracks=" << DevDiagDumpImplementation::boolStr(tw.hasTracks()) << "\n";
    o << "trackCount=" << tw.trackCount() << "\n";
    o << "readyTrackCount=" << tw.readyTrackCount() << "\n";
    o << "softcapHit=" << DevDiagDumpImplementation::boolStr(tw.softcapHit()) << "\n";
    o << "tps=" << tw.data().tps << "\n";
    o << "levelKey=" << tw.data().levelKey << "\n";
    o << "statusLine=" << baker.statusLine() << "\n";
    o << "bakerRunning=" << DevDiagDumpImplementation::boolStr(baker.running()) << "\n";
    o << fmt::format("lastBake bakedEffects={} startTimed={} skipFollow={} skipDyn={} "
                     "skipTouch={} children={} effects={} sectionExtras={} collectIncomplete={}\n",
                     baker.lastBakedEffects(), baker.lastBakedStartTimed(),
                     baker.lastSkippedFollow(), baker.lastSkippedDynamic(),
                     baker.lastSkippedTouch(), baker.lastChildCount(), baker.lastEffectCount(),
                     baker.lastSectionExtras(),
                     DevDiagDumpImplementation::boolStr(baker.lastCollectIncomplete()));
    o << "softMaxTracks=" << TriggerWorldStore::kSoftMaxTracks << "\n";
    // Kind histogram
    int kinds[16]{};
    for (auto const& t : tw.data().tracks) {
        int offset = static_cast<int>(t.kind);
        if (offset < 0 || offset >= 16)
            offset = 15;
        ++kinds[offset];
    }
    o << "kindHistogram:\n";
    for (int index = 0; index < 16; ++index) {
        if (kinds[index] <= 0)
            continue;
        o << fmt::format("  kind{}={}\n", index, kinds[index]);
    }
    return o.str();
}

bool DevDiagDump::prepareBakeEvents(PlayLayer*& outLayer, int& outSlot, float& outDt,
                                    double& outSrcTps, double& outBakeTps,
                                    std::vector<ReplayEvent>& outEv, std::string& outErr,
                                    int* outSubsteps, BakePrepStats* outStats) {
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
    BakePrepStats stats{};
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

std::string DevDiagDump::buildReplayRemap() {
    std::ostringstream o;
    PlayLayer* layer = nullptr;
    int slot = -1;
    float deltaTime = 0.f;
    double srcTps = 0.0;
    double bakeTps = 0.0;
    std::vector<ReplayEvent> ev;
    std::string err;
    BakePrepStats stats{};
    if (!prepareBakeEvents(layer, slot, deltaTime, srcTps, bakeTps, ev, err, nullptr, &stats)) {
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
    auto probe = ShadowSimulator::get().runBakeTimeline(
        layer, slot, ev, deltaTime, deathTraceUntil(-1, lastEv), -1, true, false, true, 1);
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

std::string DevDiagDump::buildBakeProbe(int maxJobs) {
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
    if (!prepareBakeEvents(layer, slot, deltaTime, srcTps, bakeTps, ev, err, &substeps)) {
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

std::string DevDiagDump::buildBakeFrameTrace() {
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
    BakePrepStats stats{};
    if (!prepareBakeEvents(layer, slot, deltaTime, srcTps, bakeTps, ev, err, &substeps, &stats)) {
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
    int const probeUntil = deathTraceUntil(-1, lastEv);
    auto probe = ShadowSimulator::get().runBakeTimeline(layer, slot, ev, deltaTime, probeUntil, -1,
                                                        true, false, true, substeps);
    int const until =
        deathTraceUntil(probe.died ? probe.deathFrame : -1, probe.died ? -1 : std::min(lastEv, 56));
    o << fmt::format(
        "=== TRACE A: frames 0..{} (deathF={} → death+8) parkReals=ON substeps={} ===\n", until,
        probe.deathFrame, substeps);
    auto firstValue = ShadowSimulator::get().runBakeTimeline(layer, slot, ev, deltaTime, until, -1,
                                                             true, true, true, substeps);
    o << fmt::format("result died={} deathF={} deathX={:.2f} deathY={:.2f} deathObjId={} "
                     "deathObjType={} deathObjGameId={} unknown={} softHits={} hardHits={} "
                     "unknownTop3=[{}] framesLogged={}\n",
                     DevDiagDumpImplementation::boolStr(firstValue.died), firstValue.deathFrame,
                     firstValue.deathX, firstValue.deathY, firstValue.deathObjId,
                     firstValue.deathObjType, firstValue.deathObjGameId,
                     DevDiagDumpImplementation::boolStr(firstValue.unknown),
                     firstValue.unknownSoftHits, firstValue.unknownHardHits,
                     formatUnknownTop3(firstValue.unknownHist), firstValue.frames.size());
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

std::string DevDiagDump::buildUnknownHistogram() {
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
    BakePrepStats stats{};
    if (!prepareBakeEvents(layer, slot, deltaTime, srcTps, bakeTps, ev, err, &substeps, &stats)) {
        o << "ERROR: " << err << "\n";
        return o.str();
    }
    int lastEv = 0;
    for (auto const& e : ev) {
        if (e.frame > lastEv)
            lastEv = e.frame;
    }
    auto probe = ShadowSimulator::get().runBakeTimeline(
        layer, slot, ev, deltaTime, deathTraceUntil(-1, lastEv), -1, true, false, true, substeps);
    int const until = deathTraceUntil(probe.died ? probe.deathFrame : -1, lastEv);
    auto run = ShadowSimulator::get().runBakeTimeline(layer, slot, ev, deltaTime, until, -1, true,
                                                      false, true, substeps);
    o << fmt::format("until={} died={} deathF={} deathObjId={} deathObjGameId={} "
                     "twTracks={} droppedP2={} shiftedMinus1={}\n",
                     until, DevDiagDumpImplementation::boolStr(run.died), run.deathFrame,
                     run.deathObjId, run.deathObjGameId, TriggerWorldStore::get().trackCount(),
                     stats.droppedP2, DevDiagDumpImplementation::boolStr(stats.shiftedMinus1));
    o << fmt::format("softHits={} hardHits={} bins={} unknownTop3=[{}]\n\n", run.unknownSoftHits,
                     run.unknownHardHits, run.unknownHist.size(),
                     formatUnknownTop3(run.unknownHist));
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

std::string DevDiagDump::buildSelfChecks() {
    std::ostringstream o;
    std::string report;
    if (PlanGoldCorpus::run(report)) {
        o << "PlanGoldCorpus: OK\n" << report << "\n";
    } else {
        o << "PlanGoldCorpus: FAIL\n" << report << "\n";
    }
    return o.str();
}

std::string DevDiagDump::buildOverlay() {
    std::ostringstream o;
    // Force refresh then read.
    AssistController::get().updateOverlay();
    o << "attemptTick=" << AssistController::get().attemptTick() << "\n";
    o << "assistTouched="
      << DevDiagDumpImplementation::boolStr(AssistController::get().assistTouchedRun()) << "\n";
    o << "(see Geode console / overlay HUD for live line)\n";
    return o.str();
}

bool DevDiagDump::dumpAll(std::filesystem::path& outDir, std::string& errMsg) {
    outDir = makeStampDir("full", errMsg);
    if (outDir.empty())
        return false;

    auto put = [&](char const* name, std::string const& body) -> bool {
        return writeFile(outDir, name, body, errMsg);
    };

    if (!put("00_INDEX.txt", buildIndex(outDir, false)))
        return false;
    if (!put("01_settings.txt", buildSettings()))
        return false;
    if (!put("02_clock_assist.txt", buildClockAssist()))
        return false;
    if (!put("02b_ghost_hitbox.txt", buildGhostHitbox()))
        return false;
    if (!put("03_replay.txt", buildReplay()))
        return false;
    if (!put("03b_replay_remap.txt", buildReplayRemap()))
        return false;
    if (!put("04_bakemap.txt", buildBakeMap()))
        return false;
    if (!put("05_trigger_world.txt", buildTriggerWorld()))
        return false;
    if (!put("06_bake_probe.txt", buildBakeProbe(48)))
        return false;
    if (!put("06b_bake_frames.txt", buildBakeFrameTrace()))
        return false;
    if (!put("06c_unknown_hist.txt", buildUnknownHistogram()))
        return false;
    if (!put("07_selfchecks.txt", buildSelfChecks()))
        return false;
    if (!put("08_overlay.txt", buildOverlay()))
        return false;

    AssistLog::info(fmt::format("DIAG: full dump → {}", outDir.string()));
    return true;
}

bool DevDiagDump::dumpBakeFail(std::filesystem::path& outDir, std::string& errMsg) {
    outDir = makeStampDir("bakefail", errMsg);
    if (outDir.empty())
        return false;

    auto put = [&](char const* name, std::string const& body) -> bool {
        return writeFile(outDir, name, body, errMsg);
    };

    // Soft per-file: bakefail must still surface an alert even if one builder
    // returns empty — never abort the whole dump after the first miss.
    int wrote = 0;
    auto softPut = [&](char const* name, std::string const& body) {
        if (put(name, body))
            ++wrote;
    };
    softPut("00_INDEX.txt", buildIndex(outDir, true));
    softPut("02_clock_assist.txt", buildClockAssist());
    // Hazard overlap uses SectionObjectWalk (poison-safe m_sections).
    softPut("02b_ghost_hitbox.txt", buildGhostHitbox());
    softPut("03_replay.txt", buildReplay());
    softPut("03b_replay_remap.txt", buildReplayRemap());
    softPut("04_bakemap.txt", buildBakeMap());
    softPut("05_trigger_world.txt", buildTriggerWorld());
    softPut("06_bake_probe.txt", buildBakeProbe(64));
    softPut("06b_bake_frames.txt", buildBakeFrameTrace());
    softPut("06c_unknown_hist.txt", buildUnknownHistogram());
    softPut("08_overlay.txt", buildOverlay());

    AssistLog::info(fmt::format("DIAG: bake-fail dump → {} (files={})", outDir.string(), wrote));
    return wrote > 0;
}
