#include "ConfigBundle.hpp"
#include "../config/StageConfig.hpp"
#include <fstream>
using namespace geode::prelude;

static void put(matjson::Value& o, char const* k, auto v) { o[k] = v; }

// Exports a portable subset spanning Window / Algorithm / Smart (not full Scope/Learning).
geode::Result<> ConfigBundle::exportTo(std::filesystem::path const& path) {
    auto* mod = Mod::get();
    matjson::Value root;
    root["version"] = 1;
    root["preset"] = mod->getSettingValue<std::string>("preset");
    root["ticks-all"] = mod->getSettingValue<int64_t>("ticks-all");
    root["ticks-wave"] = mod->getSettingValue<int64_t>("ticks-wave");
    root["death-slack"] = mod->getSettingValue<int64_t>("death-slack");
    root["assist-mode"] = mod->getSettingValue<std::string>("assist-mode");
    root["assist-orbs"] = mod->getSettingValue<bool>("assist-orbs");
    root["tick-budget"] = mod->getSettingValue<int64_t>("tick-budget");
    root["sim-budget-us"] = mod->getSettingValue<int64_t>("sim-budget-us");
    root["smart-depth"] = mod->getSettingValue<int64_t>("smart-depth");
    std::ofstream f(path);
    if (!f) return Err("export open failed");
    f << root.dump();
    return Ok();
}

geode::Result<> ConfigBundle::importFrom(std::filesystem::path const& path) {
    std::ifstream f(path);
    if (!f) return Err("import open failed");
    std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto parsed = matjson::parse(body);
    if (parsed.isErr()) return Err("import json");
    auto root = parsed.unwrap();
    auto* mod = Mod::get();
    auto applyS = [&](char const* k) {
        if (root.contains(k)) {
            try { mod->setSettingValue<std::string>(k, std::string(root[k].asString().unwrapOr(""))); } catch (...) {}
        }
    };
    auto applyI = [&](char const* k) {
        if (root.contains(k)) {
            try { mod->setSettingValue<int64_t>(k, root[k].asInt().unwrapOr(0)); } catch (...) {}
        }
    };
    auto applyB = [&](char const* k) {
        if (root.contains(k)) {
            try { mod->setSettingValue<bool>(k, root[k].asBool().unwrapOr(false)); } catch (...) {}
        }
    };
    applyS("preset"); applyS("assist-mode");
    applyI("ticks-all"); applyI("ticks-wave"); applyI("death-slack");
    applyI("tick-budget"); applyI("sim-budget-us"); applyI("smart-depth");
    applyB("assist-orbs");
    StageConfig::refreshCache();
    return Ok();
}