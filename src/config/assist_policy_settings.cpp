#include "assist_policy_settings.hpp"
#include "../core/learning/level_profile.hpp"
#include <Geode/Geode.hpp>

std::array<EbAssistPolicySettings::Policy, EbStageConfig::MODE_COUNT>
    EbAssistPolicySettings::s_chain{};
EbAssistPolicySettings::Policy EbAssistPolicySettings::s_ground = Policy::Auto;

EbAssistPolicySettings::Policy EbAssistPolicySettings::parse(std::string const& value,
                                                             Policy inherited) {
    if (value == "On")
        return Policy::On;
    if (value == "Off")
        return Policy::Off;
    if (value == "Auto")
        return Policy::Auto;
    return inherited;
}

std::string EbAssistPolicySettings::levelValue(std::string const& key) {
    auto const& level = EbLevelProfile::get().key();
    if (level.empty())
        return "Inherit";
    auto data = matjson::parse(
        geode::prelude::Mod::get()->getSettingValue<std::string>("level-assist-policies"));
    if (data.isErr())
        return "Inherit";
    auto root = data.unwrap();
    if (!root.isObject() || !root.contains(level) || !root[level].isObject() ||
        !root[level].contains(key))
        return "Inherit";
    return root[level][key].asString().unwrapOr("Inherit");
}

void EbAssistPolicySettings::setLevelValue(std::string const& key, std::string const& value) {
    auto const& level = EbLevelProfile::get().key();
    if (level.empty())
        return;
    auto* mod = geode::prelude::Mod::get();
    auto parsed = matjson::parse(mod->getSettingValue<std::string>("level-assist-policies"));
    if (parsed.isErr() || !parsed.unwrap().isObject())
        return;
    auto root = parsed.unwrap();
    if (!root.contains(level) || !root[level].isObject())
        root[level] = matjson::Value::object();
    root[level][key] = value;
    mod->setSettingValue<std::string>("level-assist-policies", root.dump());
    refresh();
}

void EbAssistPolicySettings::refresh() {
    auto* mod = geode::prelude::Mod::get();
    auto global = parse(mod->getSettingValue<std::string>("orb-chain-verify"), Policy::Auto);
    char const* modes[] = {"cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing"};
    for (int index = 0; index < EbStageConfig::MODE_COUNT; ++index) {
        std::string key = std::string(modes[index]) + "-orb-chain-verify";
        auto mode = parse(mod->getSettingValue<std::string>(key), global);
        s_chain[index] = parse(levelValue(key), mode);
    }
    s_ground = parse(levelValue("ball-ground-check"),
                     parse(mod->getSettingValue<std::string>("ball-ground-check"), Policy::Auto));
}

EbAssistPolicySettings::Policy EbAssistPolicySettings::chain(EbStageConfig::GameModeKind mode) {
    int index = static_cast<int>(mode);
    if (index < 0 || index >= EbStageConfig::MODE_COUNT)
        return Policy::Auto;
    return s_chain[index];
}

EbAssistPolicySettings::Policy EbAssistPolicySettings::ground() {
    return s_ground;
}
