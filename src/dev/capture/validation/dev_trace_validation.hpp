#ifndef DEV_TRACE_VALIDATION_HPP
#define DEV_TRACE_VALIDATION_HPP

#include <string>
#include <vector>

#include "../dev_trace_store.hpp"

class DevTraceValidation {
public:
    static bool validatePressChain(std::vector<DevDecisionEvent> const& events, int64_t pressId,
                                   std::string& err);
    static DevDecisionEvent makeEv(DevEventKind kind, int64_t eventId, int64_t attemptId,
                                   int64_t pressId, int64_t parent, int64_t planId);
    static bool selfCheck(std::string& report);

private:
    DevTraceValidation() = delete;
};

#endif
