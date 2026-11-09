#ifndef TRIGGER_WORLD_CHECKS_HPP
#define TRIGGER_WORLD_CHECKS_HPP

#include <string>

class EbTriggerWorldChecks {
public:
    static bool selfCheck(std::string& report);

private:
    EbTriggerWorldChecks() = delete;
};

#endif
