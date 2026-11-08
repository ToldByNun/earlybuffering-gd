#ifndef DEV_FIDELITY_CHECKS_HPP
#define DEV_FIDELITY_CHECKS_HPP

#include <string>

class DevFidelityChecks {
public:
    static bool selfCheck(std::string& report);

private:
    DevFidelityChecks() = delete;
};

#endif
