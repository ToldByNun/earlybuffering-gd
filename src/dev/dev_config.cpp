#include "dev_config.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#include <string>

bool DevConfigImplementation::s_devCacheInit = false;
bool DevConfigImplementation::s_devRecordEnable = false;
int DevConfigImplementation::s_devRecordCount = 10;
bool DevConfigImplementation::s_devCountEmptyAttempts = false;
bool DevConfigImplementation::s_devFreezeWorld = false;
bool DevConfigImplementation::s_devDisableRandom = true;
bool DevConfigImplementation::s_devEvalOnDeath = true;
DevConfig::EvalGrid DevConfigImplementation::s_devEvalGrid = DevConfig::EvalGrid::Band;
int DevConfigImplementation::s_devEvalHorizon = 48;
int DevConfigImplementation::s_devEvalTail = 48;
DevConfig::Objective DevConfigImplementation::s_devObjective =
    DevConfig::Objective::SurviveThenEndX;
bool DevConfigImplementation::s_devEvalRequireDeps = true;
bool DevConfigImplementation::s_devRecordCands = true;
int DevConfigImplementation::s_devEvalTickBudget = 8000;
bool DevConfigImplementation::s_devOverlay = false;

int DevConfigImplementation::clampInt(int value, int lowerBound, int upperBound) {
    return std::max(lowerBound, std::min(upperBound, value));
}

int DevConfigImplementation::readInt(geode::prelude::Mod* mod, char const* id, int fallback) {
    try {
        return static_cast<int>(mod->getSettingValue<int64_t>(id));
    } catch (...) {
        return fallback;
    }
}

bool DevConfigImplementation::readBool(geode::prelude::Mod* mod, char const* id, bool fallback) {
    try {
        return mod->getSettingValue<bool>(id);
    } catch (...) {
        return fallback;
    }
}

std::string DevConfigImplementation::readString(geode::prelude::Mod* mod, char const* id,
                                                char const* fallback) {
    try {
        return mod->getSettingValue<std::string>(id);
    } catch (...) {
        return fallback ? fallback : "";
    }
}

DevConfig::EvalGrid DevConfigImplementation::parseEvalGrid(std::string const& s) {
    if (s == "full")
        return DevConfig::EvalGrid::Full;
    if (!(s == "sparse")) {
        return DevConfig::EvalGrid::Band;
    }
    return DevConfig::EvalGrid::Sparse;
}

DevConfig::Objective DevConfigImplementation::parseObjective(std::string const& s) {
    if (s == "planner_mirror_v1")
        return DevConfig::Objective::PlannerMirror;
    if (!(s == "horizon_clear_v1")) {
        return DevConfig::Objective::SurviveThenEndX;
    }
    return DevConfig::Objective::HorizonClear;
}

void DevConfig::refreshCache() {
    auto* mod = geode::prelude::Mod::get();
    if (!mod) {
        DevConfigImplementation::s_devCacheInit = true;
        return;
    }

    DevConfigImplementation::s_devRecordEnable =
        DevConfigImplementation::readBool(mod, "dev-record-enable", false);
    DevConfigImplementation::s_devRecordCount = DevConfigImplementation::clampInt(
        DevConfigImplementation::readInt(mod, "dev-record-count", 10), 1, 50);
    DevConfigImplementation::s_devCountEmptyAttempts =
        DevConfigImplementation::readBool(mod, "dev-count-empty-attempts", false);
    // Commit 2 LOCK: freeze-world default ON for honest corpus / eval records.
    DevConfigImplementation::s_devFreezeWorld =
        DevConfigImplementation::readBool(mod, "dev-freeze-world", true);
    DevConfigImplementation::s_devDisableRandom =
        DevConfigImplementation::readBool(mod, "dev-disable-random", true);
    DevConfigImplementation::s_devEvalOnDeath =
        DevConfigImplementation::readBool(mod, "dev-eval-on-death", true);
    DevConfigImplementation::s_devEvalGrid = DevConfigImplementation::parseEvalGrid(
        DevConfigImplementation::readString(mod, "dev-eval-grid", "band"));
    DevConfigImplementation::s_devEvalHorizon = DevConfigImplementation::clampInt(
        DevConfigImplementation::readInt(mod, "dev-eval-horizon", 48), 1, 320);
    DevConfigImplementation::s_devEvalTail = DevConfigImplementation::clampInt(
        DevConfigImplementation::readInt(mod, "dev-eval-tail", 48), 0, 320);
    DevConfigImplementation::s_devObjective = DevConfigImplementation::parseObjective(
        DevConfigImplementation::readString(mod, "dev-objective", "survive_then_endx_v1"));
    DevConfigImplementation::s_devEvalRequireDeps =
        DevConfigImplementation::readBool(mod, "dev-eval-require-deps", true);
    DevConfigImplementation::s_devRecordCands =
        DevConfigImplementation::readBool(mod, "dev-record-cands", true);
    DevConfigImplementation::s_devEvalTickBudget = DevConfigImplementation::clampInt(
        DevConfigImplementation::readInt(mod, "dev-eval-tick-budget", 8000), 500, 50000);
    DevConfigImplementation::s_devOverlay =
        DevConfigImplementation::readBool(mod, "dev-overlay", false);

    DevConfigImplementation::s_devCacheInit = true;
}

void DevConfigImplementation::ensureCache() {
    if (!DevConfigImplementation::s_devCacheInit) {
        DevConfig::refreshCache();
    }
}

bool DevConfig::recordEnable() {
    DevConfigImplementation::ensureCache();
    return DevConfigImplementation::s_devRecordEnable;
}

int DevConfig::recordCount() {
    DevConfigImplementation::ensureCache();
    return DevConfigImplementation::s_devRecordCount;
}

bool DevConfig::countEmptyAttempts() {
    DevConfigImplementation::ensureCache();
    return DevConfigImplementation::s_devCountEmptyAttempts;
}

bool DevConfig::freezeWorld() {
    DevConfigImplementation::ensureCache();
    return DevConfigImplementation::s_devFreezeWorld;
}

bool DevConfig::disableRandom() {
    DevConfigImplementation::ensureCache();
    return DevConfigImplementation::s_devDisableRandom;
}

bool DevConfig::evalOnDeath() {
    DevConfigImplementation::ensureCache();
    return DevConfigImplementation::s_devEvalOnDeath;
}

DevConfig::EvalGrid DevConfig::evalGrid() {
    DevConfigImplementation::ensureCache();
    return DevConfigImplementation::s_devEvalGrid;
}

char const* DevConfig::evalGridId(EvalGrid grid) {
    switch (grid) {
    case EvalGrid::Full:
        return "full";
    case EvalGrid::Sparse:
        return "sparse";
    case EvalGrid::Band:
        break;
    }
    return "band";
}

int DevConfig::evalHorizon() {
    DevConfigImplementation::ensureCache();
    return DevConfigImplementation::s_devEvalHorizon;
}

int DevConfig::evalTail() {
    DevConfigImplementation::ensureCache();
    return DevConfigImplementation::s_devEvalTail;
}

DevConfig::Objective DevConfig::objective() {
    DevConfigImplementation::ensureCache();
    return DevConfigImplementation::s_devObjective;
}

char const* DevConfig::objectiveId(Objective object) {
    switch (object) {
    case Objective::PlannerMirror:
        return "planner_mirror_v1";
    case Objective::HorizonClear:
        return "horizon_clear_v1";
    case Objective::SurviveThenEndX:
        break;
    }
    return "survive_then_endx_v1";
}

bool DevConfig::evalRequireDeps() {
    DevConfigImplementation::ensureCache();
    return DevConfigImplementation::s_devEvalRequireDeps;
}

bool DevConfig::recordCands() {
    DevConfigImplementation::ensureCache();
    return DevConfigImplementation::s_devRecordCands;
}

int DevConfig::evalTickBudget() {
    DevConfigImplementation::ensureCache();
    return DevConfigImplementation::s_devEvalTickBudget;
}

bool DevConfig::overlay() {
    DevConfigImplementation::ensureCache();
    return DevConfigImplementation::s_devOverlay;
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
    expect(std::strcmp(objectiveId(Objective::SurviveThenEndX), "survive_then_endx_v1") == 0,
           "obj-survive-id");
    expect(std::strcmp(objectiveId(Objective::PlannerMirror), "planner_mirror_v1") == 0,
           "obj-mirror-id");
    expect(std::strcmp(objectiveId(Objective::HorizonClear), "horizon_clear_v1") == 0,
           "obj-horizon-id");

    expect(DevConfigImplementation::parseEvalGrid("full") == EvalGrid::Full, "parse-grid-full");
    expect(DevConfigImplementation::parseEvalGrid("band") == EvalGrid::Band, "parse-grid-band");
    expect(DevConfigImplementation::parseEvalGrid("sparse") == EvalGrid::Sparse,
           "parse-grid-sparse");
    expect(DevConfigImplementation::parseEvalGrid("nope") == EvalGrid::Band, "parse-grid-fallback");
    expect(DevConfigImplementation::parseObjective("survive_then_endx_v1") ==
               Objective::SurviveThenEndX,
           "parse-obj-survive");
    expect(DevConfigImplementation::parseObjective("planner_mirror_v1") == Objective::PlannerMirror,
           "parse-obj-mirror");
    expect(DevConfigImplementation::parseObjective("horizon_clear_v1") == Objective::HorizonClear,
           "parse-obj-horizon");
    expect(DevConfigImplementation::parseObjective("nope") == Objective::SurviveThenEndX,
           "parse-obj-fallback");

    expect(DevConfigImplementation::clampInt(0, 1, 50) == 1, "clamp-lo");
    expect(DevConfigImplementation::clampInt(99, 1, 50) == 50, "clamp-hi");
    expect(DevConfigImplementation::clampInt(10, 1, 50) == 10, "clamp-mid");

    // Cache refresh must leave recording off by default when settings missing / defaults.
    refreshCache();
    expect(recordCount() >= 1 && recordCount() <= 50, "record-count-range");
    expect(evalHorizon() >= 1 && evalHorizon() <= 320, "horizon-range");
    expect(evalTail() >= 0 && evalTail() <= 320, "tail-range");
    expect(evalTickBudget() >= 500, "tick-budget-min");

    if (fails == 0) {
        report = "DevConfig selfCheck OK";
    } else {
        report = fmt::format("DevConfig {} fails: {}", fails, report);
    }
    return fails == 0;
}
