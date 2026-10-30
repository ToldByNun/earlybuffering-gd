#include "ConfigBundle.hpp"
#include "ConfigBundleCodec.hpp"
#include "ConfigBundleKeys.hpp"
#include "../config/StageConfig.hpp"

#include <fstream>
#include <string>

using namespace geode::prelude;

geode::Result<> ConfigBundle::exportTo(std::filesystem::path const& path) {
    auto* mod = Mod::get();
    if (!mod) {
        return Err("export: mod unavailable");
    }

    matjson::Value settings = matjson::Value::object();

    for (auto const* it = ConfigBundleKeys::begin(); it != ConfigBundleKeys::end(); ++it) {
        auto const& entry = *it;
        try {
            switch (entry.kind) {
                case ConfigBundleKeys::Kind::Bool:
                    settings[entry.key] = mod->getSettingValue<bool>(entry.key);
                    break;
                case ConfigBundleKeys::Kind::Int:
                    settings[entry.key] = mod->getSettingValue<int64_t>(entry.key);
                    break;
                case ConfigBundleKeys::Kind::Float:
                    settings[entry.key] = mod->getSettingValue<double>(entry.key);
                    break;
                case ConfigBundleKeys::Kind::String:
                    settings[entry.key] = mod->getSettingValue<std::string>(entry.key);
                    break;
                case ConfigBundleKeys::Kind::Color: {
                    auto const color = mod->getSettingValue<cocos2d::ccColor3B>(entry.key);
                    settings[entry.key] = ConfigBundleCodec::colorToHex(color);
                    break;
                }
                case ConfigBundleKeys::Kind::Keybind:
                    // Keybind export/import lands in Commit 4 (vector<Keybind> → string).
                    break;
                default:
                    break;
            }
        }
        catch (...) {
            // Skip one bad key; continue exporting the rest.
        }
    }

    matjson::Value root;
    root["version"] = ConfigBundle::kFormatVersion;
    root["settings"] = settings;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return Err("export open failed");
    }
    out << root.dump();
    if (!out) {
        return Err("export write failed");
    }
    return Ok();
}

geode::Result<> ConfigBundle::importFrom(std::filesystem::path const& path) {
    // Commit 3 replaces this with full v2 import + applying guard.
    // Keep v1 flat-root behavior until then so Export→old Import still works for scalars.
    std::ifstream f(path);
    if (!f) return Err("import open failed");
    std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto parsed = matjson::parse(body);
    if (parsed.isErr()) return Err("import json");
    auto root = parsed.unwrap();
    auto* mod = Mod::get();
    if (!mod) return Err("import: mod unavailable");

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
    applyS("preset"); applyS("assist-mode"); applyS("assist-pick");
    applyI("ticks-all"); applyI("ticks-wave"); applyI("death-slack");
    applyI("tick-budget"); applyI("sim-budget-us"); applyI("smart-depth");
    applyI("robust-k");
    applyB("assist-orbs"); applyB("robust-jitter");
    StageConfig::refreshCache();
    return Ok();
}
