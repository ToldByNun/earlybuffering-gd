#ifndef PAUSE_CONFIG_ACTIONS_HPP
#define PAUSE_CONFIG_ACTIONS_HPP

#include <string>

class EbPauseConfigActions {
public:
    static void exportConfig();
    static void importConfig();
    static void openCloudConfigs();
    static void afterConfigApplied(std::string const& successMsg);

private:
    EbPauseConfigActions() = delete;
};

#endif
