#ifndef CLOUD_BROWSER_LIST_HPP
#define CLOUD_BROWSER_LIST_HPP

#include "../../../cloud/catalog/cloud_config_index.hpp"
#include <vector>

#include "../cloud_config_popup.hpp"

class CloudBrowserList {
public:
    static bool packMatchesSearch(CloudConfigPopup const& owner,
                                  CloudConfigIndex::PackEntry const& e);
    static char const* sortModeLabel(CloudConfigPopup const& owner);
    static void updateSortLabel(CloudConfigPopup& owner);
    static void onSortCycle(CloudConfigPopup& owner, geode::prelude::CCObject*);
    static void applySort(CloudConfigPopup const& owner,
                          std::vector<CloudConfigIndex::PackEntry const*>& rows);
    static void rebuildList(CloudConfigPopup& owner);

private:
    CloudBrowserList() = delete;
};

#endif
