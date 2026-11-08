#ifndef DEV_REJECT_CHECKS_HPP
#define DEV_REJECT_CHECKS_HPP

#include <string>

class DevRejectChecks {
public:
    static bool selfCheck(std::string& report);

private:
    DevRejectChecks() = delete;
};

#endif
