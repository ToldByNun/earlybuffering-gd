#include "dev_diag_dump.hpp"
#include "../../core/assist/assist_controller.hpp"
#include "../../core/bundle/config_bundle_keys.hpp"
#include "../../core/replay/replay_import.hpp"
#include "../../core/sim/shadow_simulator.hpp"
#include "../../debug/assist_log.hpp"
#include "../../debug/plan_gold_corpus.hpp"
#include "diagnostics/dev_bake_diagnostics.hpp"
#include "diagnostics/dev_ghost_diagnostics.hpp"
#include "diagnostics/dev_state_diagnostics.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

std::string EbDevDiagDumpImplementation::stampNow() {
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

bool EbDevDiagDumpImplementation::writeTextFile(std::filesystem::path const& path,
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

char const* EbDevDiagDumpImplementation::boolStr(bool value) {
    return value ? "true" : "false";
}

std::string EbDevDiagDumpImplementation::settingValueLine(EbConfigBundleKeys::Entry const& e) {
    auto* mod = geode::prelude::Mod::get();
    if (!mod)
        return fmt::format("{}=<no-mod>", e.key);
    try {
        switch (e.kind) {
        case EbConfigBundleKeys::Kind::Bool:
            return fmt::format(
                "{}={} (bool)", e.key,
                EbDevDiagDumpImplementation::boolStr(mod->getSettingValue<bool>(e.key)));
        case EbConfigBundleKeys::Kind::Int:
            return fmt::format("{}={} (int)", e.key, mod->getSettingValue<int64_t>(e.key));
        case EbConfigBundleKeys::Kind::Float:
            return fmt::format("{}={} (float)", e.key, mod->getSettingValue<double>(e.key));
        case EbConfigBundleKeys::Kind::String:
            return fmt::format("{}=\"{}\" (string)", e.key,
                               mod->getSettingValue<std::string>(e.key));
        case EbConfigBundleKeys::Kind::Color: {
            auto candidate = mod->getSettingValue<cocos2d::ccColor3B>(e.key);
            return fmt::format("{}=#{:02X}{:02X}{:02X} (color)", e.key, candidate.r, candidate.g,
                               candidate.b);
        }
        case EbConfigBundleKeys::Kind::Keybind:
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

std::filesystem::path EbDevDiagDump::makeStampDir(char const* prefix, std::string& errMsg) {
    errMsg.clear();
    auto* mod = geode::prelude::Mod::get();
    if (!mod) {
        errMsg = "No Mod instance";
        return {};
    }
    auto const root = mod->getSaveDir() / "diag";
    auto const directory = root / fmt::format("{}_{}", prefix ? prefix : "diag",
                                              EbDevDiagDumpImplementation::stampNow());
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        errMsg = fmt::format("mkdir failed: {}", ec.message());
        return {};
    }
    return directory;
}

bool EbDevDiagDump::writeFile(std::filesystem::path const& dir, char const* name,
                            std::string const& body, std::string& errMsg) {
    auto const path = dir / name;
    if (!EbDevDiagDumpImplementation::writeTextFile(path, body)) {
        errMsg = fmt::format("write failed: {}", path.string());
        return false;
    }
    return true;
}

std::string EbDevDiagDump::buildIndex(std::filesystem::path const& dir, bool bakeFailOnly) {
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

int EbDevDiagDump::deathTraceUntil(int deathFrame, int lastEventFrame) {
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
EbDevDiagDump::formatUnknownTop3(std::vector<EbShadowSimulator::UnknownHistBin> const& hist) {
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

bool EbDevDiagDump::selfCheck(std::string& report) {
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

    std::vector<EbShadowSimulator::UnknownHistBin> empty;
    expect(formatUnknownTop3(empty) == "-", "top3-empty");
    std::vector<EbShadowSimulator::UnknownHistBin> bins = {
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

std::string EbDevDiagDump::buildSettings() {
    return EbDevStateDiagnostics::buildSettings();
}

void EbDevDiagDumpImplementation::dumpVehicleFlags(std::ostringstream& o, char const* prefix,
                                                 PlayerObject* player) {
    return EbDevGhostDiagnostics::dumpVehicleFlags(o, prefix, player);
}

std::string EbDevDiagDump::buildClockAssist() {
    return EbDevStateDiagnostics::buildClockAssist();
}

bool EbDevDiagDumpImplementation::isHazardType(GameObject* object) {
    return EbDevGhostDiagnostics::isHazardType(object);
}

void EbDevDiagDumpImplementation::dumpRectLine(std::ostringstream& o, char const* name,
                                             PlayerObject* player) {
    return EbDevGhostDiagnostics::dumpRectLine(o, name, player);
}

int EbDevDiagDumpImplementation::countHazardOverlaps(PlayLayer* layer, PlayerObject* player,
                                                   std::ostringstream& detail, int maxDetail) {
    return EbDevGhostDiagnostics::countHazardOverlaps(layer, player, detail, maxDetail);
}

std::string EbDevDiagDump::buildGhostHitbox() {
    return EbDevGhostDiagnostics::buildGhostHitbox();
}

std::string EbDevDiagDump::buildReplay() {
    return EbDevStateDiagnostics::buildReplay();
}

std::string EbDevDiagDump::buildBakeMap() {
    return EbDevStateDiagnostics::buildBakeMap();
}

std::string EbDevDiagDump::buildTriggerWorld() {
    return EbDevStateDiagnostics::buildTriggerWorld();
}

bool EbDevDiagDump::prepareBakeEvents(PlayLayer*& outLayer, int& outSlot, float& outDt,
                                    double& outSrcTps, double& outBakeTps,
                                    std::vector<EbReplayEvent>& outEv, std::string& outErr,
                                    int* outSubsteps, BakePrepStats* outStats) {
    return EbDevBakeDiagnostics::prepareBakeEvents(outLayer, outSlot, outDt, outSrcTps, outBakeTps,
                                                 outEv, outErr, outSubsteps, outStats);
}

std::string EbDevDiagDump::buildReplayRemap() {
    return EbDevBakeDiagnostics::buildReplayRemap();
}

std::string EbDevDiagDump::buildBakeProbe(int maxJobs) {
    return EbDevBakeDiagnostics::buildBakeProbe(maxJobs);
}

std::string EbDevDiagDump::buildBakeFrameTrace() {
    return EbDevBakeDiagnostics::buildBakeFrameTrace();
}

std::string EbDevDiagDump::buildUnknownHistogram() {
    return EbDevBakeDiagnostics::buildUnknownHistogram();
}

std::string EbDevDiagDump::buildSelfChecks() {
    std::ostringstream o;
    std::string report;
    if (EbPlanGoldCorpus::run(report)) {
        o << "PlanGoldCorpus: OK\n" << report << "\n";
    } else {
        o << "PlanGoldCorpus: FAIL\n" << report << "\n";
    }
    return o.str();
}

std::string EbDevDiagDump::buildOverlay() {
    std::ostringstream o;
    // Force refresh then read.
    EbAssistController::get().updateOverlay();
    o << "attemptTick=" << EbAssistController::get().attemptTick() << "\n";
    o << "assistTouched="
      << EbDevDiagDumpImplementation::boolStr(EbAssistController::get().assistTouchedRun()) << "\n";
    o << "(see Geode console / overlay HUD for live line)\n";
    return o.str();
}

bool EbDevDiagDump::dumpAll(std::filesystem::path& outDir, std::string& errMsg) {
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

    EbAssistLog::info(fmt::format("DIAG: full dump → {}", outDir.string()));
    return true;
}

bool EbDevDiagDump::dumpBakeFail(std::filesystem::path& outDir, std::string& errMsg) {
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

    EbAssistLog::info(fmt::format("DIAG: bake-fail dump → {} (files={})", outDir.string(), wrote));
    return wrote > 0;
}
