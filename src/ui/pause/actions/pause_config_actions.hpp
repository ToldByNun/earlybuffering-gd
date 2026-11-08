#ifndef PAUSE_CONFIG_ACTIONS_HPP
#define PAUSE_CONFIG_ACTIONS_HPP

#include <string>

class PauseConfigActions {
public:
    static void exportConfig();
    static void importConfig();
    static void openCloudConfigs();
    static void afterConfigApplied(std::string const& successMsg);

private:
    PauseConfigActions() = delete;
};

#endif
