#ifndef DEV_REJECT_CHECKS_HPP
#define DEV_REJECT_CHECKS_HPP

#include <string>

class EbDevRejectChecks {
public:
    static bool selfCheck(std::string& report);

private:
    EbDevRejectChecks() = delete;
};

#endif
