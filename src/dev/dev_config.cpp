#include "dev_config.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#include <string>

bool EbDevConfigImplementation::s_devCacheInit = false;
bool EbDevConfigImplementation::s_devRecordEnable = false;
int EbDevConfigImplementation::s_devRecordCount = 10;
bool EbDevConfigImplementation::s_devCountEmptyAttempts = false;
bool EbDevConfigImplementation::s_devFreezeWorld = false;
bool EbDevConfigImplementation::s_devDisableRandom = true;
bool EbDevConfigImplementation::s_devEvalOnDeath = true;
EbDevConfig::EvalGrid EbDevConfigImplementation::s_devEvalGrid = EbDevConfig::EvalGrid::Band;
int EbDevConfigImplementation::s_devEvalHorizon = 48;
int EbDevConfigImplementation::s_devEvalTail = 48;
EbDevConfig::Objective EbDevConfigImplementation::s_devObjective =
    EbDevConfig::Objective::SurviveThenEndX;
bool EbDevConfigImplementation::s_devEvalRequireDeps = true;
bool EbDevConfigImplementation::s_devRecordCands = true;
int EbDevConfigImplementation::s_devEvalTickBudget = 8000;
bool EbDevConfigImplementation::s_devOverlay = false;

int EbDevConfigImplementation::clampInt(int value, int lowerBound, int upperBound) {
    return std::max(lowerBound, std::min(upperBound, value));
}

int EbDevConfigImplementation::readInt(geode::prelude::Mod* mod, char const* id, int fallback) {
    try {
        return static_cast<int>(mod->getSettingValue<int64_t>(id));
    } catch (...) {
        return fallback;
    }
}

bool EbDevConfigImplementation::readBool(geode::prelude::Mod* mod, char const* id, bool fallback) {
    try {
        return mod->getSettingValue<bool>(id);
    } catch (...) {
        return fallback;
    }
}

std::string EbDevConfigImplementation::readString(geode::prelude::Mod* mod, char const* id,
                                                char const* fallback) {
    try {
        return mod->getSettingValue<std::string>(id);
    } catch (...) {
        return fallback ? fallback : "";
    }
}

EbDevConfig::EvalGrid EbDevConfigImplementation::parseEvalGrid(std::string const& s) {
    if (s == "full")
        return EbDevConfig::EvalGrid::Full;
    if (!(s == "sparse")) {
        return EbDevConfig::EvalGrid::Band;
    }
    return EbDevConfig::EvalGrid::Sparse;
}

EbDevConfig::Objective EbDevConfigImplementation::parseObjective(std::string const& s) {
    if (s == "planner_mirror_v1")
        return EbDevConfig::Objective::PlannerMirror;
    if (!(s == "horizon_clear_v1")) {
        return EbDevConfig::Objective::SurviveThenEndX;
    }
    return EbDevConfig::Objective::HorizonClear;
}

void EbDevConfig::refreshCache() {
    auto* mod = geode::prelude::Mod::get();
    if (!mod) {
        EbDevConfigImplementation::s_devCacheInit = true;
        return;
    }

    EbDevConfigImplementation::s_devRecordEnable =
        EbDevConfigImplementation::readBool(mod, "dev-record-enable", false);
    EbDevConfigImplementation::s_devRecordCount = EbDevConfigImplementation::clampInt(
        EbDevConfigImplementation::readInt(mod, "dev-record-count", 10), 1, 50);
    EbDevConfigImplementation::s_devCountEmptyAttempts =
        EbDevConfigImplementation::readBool(mod, "dev-count-empty-attempts", false);
    // Commit 2 LOCK: freeze-world default ON for honest corpus / eval records.
    EbDevConfigImplementation::s_devFreezeWorld =
        EbDevConfigImplementation::readBool(mod, "dev-freeze-world", true);
    EbDevConfigImplementation::s_devDisableRandom =
        EbDevConfigImplementation::readBool(mod, "dev-disable-random", true);
    EbDevConfigImplementation::s_devEvalOnDeath =
        EbDevConfigImplementation::readBool(mod, "dev-eval-on-death", true);
    EbDevConfigImplementation::s_devEvalGrid = EbDevConfigImplementation::parseEvalGrid(
        EbDevConfigImplementation::readString(mod, "dev-eval-grid", "band"));
    EbDevConfigImplementation::s_devEvalHorizon = EbDevConfigImplementation::clampInt(
        EbDevConfigImplementation::readInt(mod, "dev-eval-horizon", 48), 1, 320);
    EbDevConfigImplementation::s_devEvalTail = EbDevConfigImplementation::clampInt(
        EbDevConfigImplementation::readInt(mod, "dev-eval-tail", 48), 0, 320);
    EbDevConfigImplementation::s_devObjective = EbDevConfigImplementation::parseObjective(
        EbDevConfigImplementation::readString(mod, "dev-objective", "survive_then_endx_v1"));
    EbDevConfigImplementation::s_devEvalRequireDeps =
        EbDevConfigImplementation::readBool(mod, "dev-eval-require-deps", true);
    EbDevConfigImplementation::s_devRecordCands =
        EbDevConfigImplementation::readBool(mod, "dev-record-cands", true);
    EbDevConfigImplementation::s_devEvalTickBudget = EbDevConfigImplementation::clampInt(
        EbDevConfigImplementation::readInt(mod, "dev-eval-tick-budget", 8000), 500, 50000);
    EbDevConfigImplementation::s_devOverlay =
        EbDevConfigImplementation::readBool(mod, "dev-overlay", false);

    EbDevConfigImplementation::s_devCacheInit = true;
}

void EbDevConfigImplementation::ensureCache() {
    if (!EbDevConfigImplementation::s_devCacheInit) {
        EbDevConfig::refreshCache();
    }
}

bool EbDevConfig::recordEnable() {
    EbDevConfigImplementation::ensureCache();
    return EbDevConfigImplementation::s_devRecordEnable;
}

int EbDevConfig::recordCount() {
    EbDevConfigImplementation::ensureCache();
    return EbDevConfigImplementation::s_devRecordCount;
}

bool EbDevConfig::countEmptyAttempts() {
    EbDevConfigImplementation::ensureCache();
    return EbDevConfigImplementation::s_devCountEmptyAttempts;
}

bool EbDevConfig::freezeWorld() {
    EbDevConfigImplementation::ensureCache();
    return EbDevConfigImplementation::s_devFreezeWorld;
}

bool EbDevConfig::disableRandom() {
    EbDevConfigImplementation::ensureCache();
    return EbDevConfigImplementation::s_devDisableRandom;
}

bool EbDevConfig::evalOnDeath() {
    EbDevConfigImplementation::ensureCache();
    return EbDevConfigImplementation::s_devEvalOnDeath;
}

EbDevConfig::EvalGrid EbDevConfig::evalGrid() {
    EbDevConfigImplementation::ensureCache();
    return EbDevConfigImplementation::s_devEvalGrid;
}

char const* EbDevConfig::evalGridId(EvalGrid grid) {
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

int EbDevConfig::evalHorizon() {
    EbDevConfigImplementation::ensureCache();
    return EbDevConfigImplementation::s_devEvalHorizon;
}

int EbDevConfig::evalTail() {
    EbDevConfigImplementation::ensureCache();
    return EbDevConfigImplementation::s_devEvalTail;
}

EbDevConfig::Objective EbDevConfig::objective() {
    EbDevConfigImplementation::ensureCache();
    return EbDevConfigImplementation::s_devObjective;
}

char const* EbDevConfig::objectiveId(Objective object) {
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

bool EbDevConfig::evalRequireDeps() {
    EbDevConfigImplementation::ensureCache();
    return EbDevConfigImplementation::s_devEvalRequireDeps;
}

bool EbDevConfig::recordCands() {
    EbDevConfigImplementation::ensureCache();
    return EbDevConfigImplementation::s_devRecordCands;
}

int EbDevConfig::evalTickBudget() {
    EbDevConfigImplementation::ensureCache();
    return EbDevConfigImplementation::s_devEvalTickBudget;
}

bool EbDevConfig::overlay() {
    EbDevConfigImplementation::ensureCache();
    return EbDevConfigImplementation::s_devOverlay;
}

bool EbDevConfig::selfCheck(std::string& report) {
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

    expect(EbDevConfigImplementation::parseEvalGrid("full") == EvalGrid::Full, "parse-grid-full");
    expect(EbDevConfigImplementation::parseEvalGrid("band") == EvalGrid::Band, "parse-grid-band");
    expect(EbDevConfigImplementation::parseEvalGrid("sparse") == EvalGrid::Sparse,
           "parse-grid-sparse");
    expect(EbDevConfigImplementation::parseEvalGrid("nope") == EvalGrid::Band, "parse-grid-fallback");
    expect(EbDevConfigImplementation::parseObjective("survive_then_endx_v1") ==
               Objective::SurviveThenEndX,
           "parse-obj-survive");
    expect(EbDevConfigImplementation::parseObjective("planner_mirror_v1") == Objective::PlannerMirror,
           "parse-obj-mirror");
    expect(EbDevConfigImplementation::parseObjective("horizon_clear_v1") == Objective::HorizonClear,
           "parse-obj-horizon");
    expect(EbDevConfigImplementation::parseObjective("nope") == Objective::SurviveThenEndX,
           "parse-obj-fallback");

    expect(EbDevConfigImplementation::clampInt(0, 1, 50) == 1, "clamp-lo");
    expect(EbDevConfigImplementation::clampInt(99, 1, 50) == 50, "clamp-hi");
    expect(EbDevConfigImplementation::clampInt(10, 1, 50) == 10, "clamp-mid");

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
