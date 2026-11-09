#ifndef DEV_ATTRIBUTION_HPP
#define DEV_ATTRIBUTION_HPP

#include <vector>

#include "../dev_reject_ledger.hpp"

class EbDevAttribution {
public:
    static std::vector<EbDevRejectLedger::AttributionNote>
    buildAttribution(EbDevRejectLedger::AttributionInput const& in);

private:
    EbDevAttribution() = delete;
};

#endif
