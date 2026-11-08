#ifndef DEV_STATE_DIAGNOSTICS_HPP
#define DEV_STATE_DIAGNOSTICS_HPP

#include <string>

class DevStateDiagnostics {
public:
    static std::string buildSettings();
    static std::string buildClockAssist();
    static std::string buildReplay();
    static std::string buildBakeMap();
    static std::string buildTriggerWorld();

private:
    DevStateDiagnostics() = delete;
};

#endif
