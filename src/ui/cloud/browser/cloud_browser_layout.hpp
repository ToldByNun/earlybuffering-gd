#ifndef CLOUD_BROWSER_LAYOUT_HPP
#define CLOUD_BROWSER_LAYOUT_HPP

#include "../cloud_config_popup.hpp"

class CloudBrowserLayout {
public:
    static bool init(CloudConfigPopup& owner);

private:
    CloudBrowserLayout() = delete;
};

#endif
