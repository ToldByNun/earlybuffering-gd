#ifndef DEV_ATTRIBUTION_HPP
#define DEV_ATTRIBUTION_HPP

#include <vector>

#include "../dev_reject_ledger.hpp"

class DevAttribution {
public:
    static std::vector<DevRejectLedger::AttributionNote>
    buildAttribution(DevRejectLedger::AttributionInput const& in);

private:
    DevAttribution() = delete;
};

#endif
