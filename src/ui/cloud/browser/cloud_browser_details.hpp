#ifndef CLOUD_BROWSER_DETAILS_HPP
#define CLOUD_BROWSER_DETAILS_HPP

#include "../cloud_config_popup.hpp"

class EbCloudBrowserDetails {
public:
    static void updateRateUi(EbCloudConfigPopup& owner);
    static void updateDetail(EbCloudConfigPopup& owner);

private:
    EbCloudBrowserDetails() = delete;
};

#endif
