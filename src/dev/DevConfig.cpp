#include "DevConfig.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#include <string>

using namespace geode::prelude;

bool g_devCacheInit = false;
bool g_devRecordEnable = false;
int g_devRecordCount = 10;
bool g_devCountEmptyAttempts = false;
bool g_devFreezeWorld = false;
bool g_devDisableRandom = true;
bool g_devEvalOnDeath = true;
DevConfig::EvalGrid g_devEvalGrid = DevConfig::EvalGrid::Band;
int g_devEvalHorizon = 48;
int g_devEvalTail = 48;
DevConfig::Objective g_devObjective = DevConfig::Objective::SurviveThenEndX;
bool g_devEvalRequireDeps = true;
bool g_devRecordCands = true;
int g_devEvalTickBudget = 8000;
bool g_devOverlay = false;

static int clampInt(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

static int readInt(Mod* mod, char const* id, int fallback) {
    try {
        return static_cast<int>(mod->getSettingValue<int64_t>(id));
    }
    catch (...) {
        return fallback;
    }
}

static bool readBool(Mod* mod, char const* id, bool fallback) {
    try {
        return mod->getSettingValue<bool>(id);
    }
    catch (...) {
        return fallback;
    }
}

static std::string readString(Mod* mod, char const* id, char const* fallback) {
    try {
        return mod->getSettingValue<std::string>(id);
    }
    catch (...) {
        return fallback ? fallback : "";
    }
}

static DevConfig::EvalGrid parseEvalGrid(std::string const& s) {
    if (s == "full") return DevConfig::EvalGrid::Full;
    if (s == "sparse") return DevConfig::EvalGrid::Sparse;
    return DevConfig::EvalGrid::Band;
}

static DevConfig::Objective parseObjective(std::string const& s) {
    if (s == "planner_mirror_v1") return DevConfig::Objective::PlannerMirror;
    if (s == "horizon_clear_v1") return DevConfig::Objective::HorizonClear;
    return DevConfig::Objective::SurviveThenEndX;
}

void DevConfig::refreshCache() {
    auto* mod = Mod::get();
    if (!mod) {
        g_devCacheInit = true;
        return;
    }

    g_devRecordEnable = readBool(mod, "dev-record-enable", false);
    g_devRecordCount = clampInt(readInt(mod, "dev-record-count", 10), 1, 50);
    g_devCountEmptyAttempts = readBool(mod, "dev-count-empty-attempts", false);
    // Commit 2 LOCK: freeze-world default ON for honest corpus / eval records.
    g_devFreezeWorld = readBool(mod, "dev-freeze-world", true);
    g_devDisableRandom = readBool(mod, "dev-disable-random", true);
    g_devEvalOnDeath = readBool(mod, "dev-eval-on-death", true);
    g_devEvalGrid = parseEvalGrid(readString(mod, "dev-eval-grid", "band"));
    g_devEvalHorizon = clampInt(readInt(mod, "dev-eval-horizon", 48), 1, 320);
    g_devEvalTail = clampInt(readInt(mod, "dev-eval-tail", 48), 0, 320);
    g_devObjective = parseObjective(readString(mod, "dev-objective", "survive_then_endx_v1"));
    g_devEvalRequireDeps = readBool(mod, "dev-eval-require-deps", true);
    g_devRecordCands = readBool(mod, "dev-record-cands", true);
    g_devEvalTickBudget = clampInt(readInt(mod, "dev-eval-tick-budget", 8000), 500, 50000);
    g_devOverlay = readBool(mod, "dev-overlay", false);

    g_devCacheInit = true;
}

static void ensureCache() {
    if (!g_devCacheInit) {
        DevConfig::refreshCache();
    }
}

bool DevConfig::recordEnable() {
    ensureCache();
    return g_devRecordEnable;
}

int DevConfig::recordCount() {
    ensureCache();
    return g_devRecordCount;
}

bool DevConfig::countEmptyAttempts() {
    ensureCache();
    return g_devCountEmptyAttempts;
}

bool DevConfig::freezeWorld() {
    ensureCache();
    return g_devFreezeWorld;
}

bool DevConfig::disableRandom() {
    ensureCache();
    return g_devDisableRandom;
}

bool DevConfig::evalOnDeath() {
    ensureCache();
    return g_devEvalOnDeath;
}

DevConfig::EvalGrid DevConfig::evalGrid() {
    ensureCache();
    return g_devEvalGrid;
}

char const* DevConfig::evalGridId(EvalGrid grid) {
    switch (grid) {
        case EvalGrid::Full: return "full";
        case EvalGrid::Sparse: return "sparse";
        case EvalGrid::Band: break;
    }
    return "band";
}

int DevConfig::evalHorizon() {
    ensureCache();
    return g_devEvalHorizon;
}

int DevConfig::evalTail() {
    ensureCache();
    return g_devEvalTail;
}

DevConfig::Objective DevConfig::objective() {
    ensureCache();
    return g_devObjective;
}

char const* DevConfig::objectiveId(Objective obj) {
    switch (obj) {
        case Objective::PlannerMirror: return "planner_mirror_v1";
        case Objective::HorizonClear: return "horizon_clear_v1";
        case Objective::SurviveThenEndX: break;
    }
    return "survive_then_endx_v1";
}

bool DevConfig::evalRequireDeps() {
    ensureCache();
    return g_devEvalRequireDeps;
}

bool DevConfig::recordCands() {
    ensureCache();
    return g_devRecordCands;
}

int DevConfig::evalTickBudget() {
    ensureCache();
    return g_devEvalTickBudget;
}

bool DevConfig::overlay() {
    ensureCache();
    return g_devOverlay;
}

bool DevConfig::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(std::strcmp(evalGridId(EvalGrid::Full), "full") == 0, "grid-full-id");
    expect(std::strcmp(evalGridId(EvalGrid::Band), "band") == 0, "grid-band-id");
    expect(std::strcmp(evalGridId(EvalGrid::Sparse), "sparse") == 0, "grid-sparse-id");
    expect(std::strcmp(objectiveId(Objective::SurviveThenEndX), "survive_then_endx_v1") == 0, "obj-survive-id");
    expect(std::strcmp(objectiveId(Objective::PlannerMirror), "planner_mirror_v1") == 0, "obj-mirror-id");
    expect(std::strcmp(objectiveId(Objective::HorizonClear), "horizon_clear_v1") == 0, "obj-horizon-id");

    expect(parseEvalGrid("full") == EvalGrid::Full, "parse-grid-full");
    expect(parseEvalGrid("band") == EvalGrid::Band, "parse-grid-band");
    expect(parseEvalGrid("sparse") == EvalGrid::Sparse, "parse-grid-sparse");
    expect(parseEvalGrid("nope") == EvalGrid::Band, "parse-grid-fallback");
    expect(parseObjective("survive_then_endx_v1") == Objective::SurviveThenEndX, "parse-obj-survive");
    expect(parseObjective("planner_mirror_v1") == Objective::PlannerMirror, "parse-obj-mirror");
    expect(parseObjective("horizon_clear_v1") == Objective::HorizonClear, "parse-obj-horizon");
    expect(parseObjective("nope") == Objective::SurviveThenEndX, "parse-obj-fallback");

    expect(clampInt(0, 1, 50) == 1, "clamp-lo");
    expect(clampInt(99, 1, 50) == 50, "clamp-hi");
    expect(clampInt(10, 1, 50) == 10, "clamp-mid");

    // Cache refresh must leave recording off by default when settings missing / defaults.
    refreshCache();
    expect(recordCount() >= 1 && recordCount() <= 50, "record-count-range");
    expect(evalHorizon() >= 1 && evalHorizon() <= 320, "horizon-range");
    expect(evalTail() >= 0 && evalTail() <= 320, "tail-range");
    expect(evalTickBudget() >= 500, "tick-budget-min");

    if (fails == 0) {
        report = "DevConfig selfCheck OK";
    }
    else {
        report = fmt::format("DevConfig {} fails: {}", fails, report);
    }
    return fails == 0;
}
