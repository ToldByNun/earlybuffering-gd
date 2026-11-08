#ifndef CLOUD_BROWSER_DETAILS_HPP
#define CLOUD_BROWSER_DETAILS_HPP

#include "../cloud_config_popup.hpp"

class CloudBrowserDetails {
public:
    static void updateRateUi(CloudConfigPopup& owner);
    static void updateDetail(CloudConfigPopup& owner);

private:
    CloudBrowserDetails() = delete;
};

#endif
