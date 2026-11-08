#include "dev_state_diagnostics.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../core/assist/assist_clock.hpp"
#include "../../../core/assist/assist_controller.hpp"
#include "../../../core/bake/bake_map.hpp"
#include "../../../core/bundle/config_bundle_keys.hpp"
#include "../../../core/sim/shadow_simulator.hpp"
#include "../../../core/trigger_world/trigger_world_baker.hpp"
#include "../../../core/trigger_world/trigger_world_store.hpp"
#include "../../dev_config.hpp"
#include "../dev_diag_dump.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

std::string DevStateDiagnostics::buildSettings() {
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

std::string DevStateDiagnostics::buildClockAssist() {
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

std::string DevStateDiagnostics::buildReplay() {
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

std::string DevStateDiagnostics::buildBakeMap() {
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

std::string DevStateDiagnostics::buildTriggerWorld() {
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
