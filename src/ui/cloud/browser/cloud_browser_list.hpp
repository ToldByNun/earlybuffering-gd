#ifndef CLOUD_BROWSER_LIST_HPP
#define CLOUD_BROWSER_LIST_HPP

#include "../../../cloud/catalog/cloud_config_index.hpp"
#include <vector>

#include "../cloud_config_popup.hpp"

class EbCloudBrowserList {
public:
    static bool packMatchesSearch(EbCloudConfigPopup const& owner,
                                  EbCloudConfigIndex::PackEntry const& e);
    static char const* sortModeLabel(EbCloudConfigPopup const& owner);
    static void updateSortLabel(EbCloudConfigPopup& owner);
    static void onSortCycle(EbCloudConfigPopup& owner, geode::prelude::CCObject*);
    static void applySort(EbCloudConfigPopup const& owner,
                          std::vector<EbCloudConfigIndex::PackEntry const*>& rows);
    static void rebuildList(EbCloudConfigPopup& owner);

private:
    EbCloudBrowserList() = delete;
};

#endif
