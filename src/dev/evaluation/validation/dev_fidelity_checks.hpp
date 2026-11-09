#ifndef DEV_FIDELITY_CHECKS_HPP
#define DEV_FIDELITY_CHECKS_HPP

#include <string>

class EbDevFidelityChecks {
public:
    static bool selfCheck(std::string& report);

private:
    EbDevFidelityChecks() = delete;
};

#endif
