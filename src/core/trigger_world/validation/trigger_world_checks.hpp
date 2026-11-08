#ifndef TRIGGER_WORLD_CHECKS_HPP
#define TRIGGER_WORLD_CHECKS_HPP

#include <string>

class TriggerWorldChecks {
public:
    static bool selfCheck(std::string& report);

private:
    TriggerWorldChecks() = delete;
};

#endif
