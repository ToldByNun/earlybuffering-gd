#ifndef CLOUD_BROWSER_LAYOUT_HPP
#define CLOUD_BROWSER_LAYOUT_HPP

#include "../cloud_config_popup.hpp"

class EbCloudBrowserLayout {
public:
    static bool init(EbCloudConfigPopup& owner);

private:
    EbCloudBrowserLayout() = delete;
};

#endif
