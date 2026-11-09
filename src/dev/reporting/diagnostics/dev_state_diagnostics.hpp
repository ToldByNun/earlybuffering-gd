#ifndef DEV_STATE_DIAGNOSTICS_HPP
#define DEV_STATE_DIAGNOSTICS_HPP

#include <string>

class EbDevStateDiagnostics {
public:
    static std::string buildSettings();
    static std::string buildClockAssist();
    static std::string buildReplay();
    static std::string buildBakeMap();
    static std::string buildTriggerWorld();

private:
    EbDevStateDiagnostics() = delete;
};

#endif
