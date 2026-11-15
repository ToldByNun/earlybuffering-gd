#ifndef ASSIST_POLICY_SETTINGS_HPP
#define ASSIST_POLICY_SETTINGS_HPP

#include "stage_config.hpp"
#include <array>
#include <string>

class EbAssistPolicySettings {
public:
    enum class Policy { Auto, On, Off };
    static void refresh();
    static Policy chain(EbStageConfig::GameModeKind mode);
    static Policy ground();
    static std::string levelValue(std::string const& key);
    static void setLevelValue(std::string const& key, std::string const& value);

private:
    static Policy parse(std::string const& value, Policy inherited);
    static std::array<Policy, EbStageConfig::MODE_COUNT> s_chain;
    static Policy s_ground;
};

#endif
