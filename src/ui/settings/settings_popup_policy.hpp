#ifndef SETTINGS_POPUP_POLICY_HPP
#define SETTINGS_POPUP_POLICY_HPP

#include <string_view>

class EbSettingsPopupPolicy {
public:
    static bool ownsPopup(std::string_view title, std::string_view modName, bool hasOwnedSettings,
                          bool hasForeignSettings);

private:
};

#endif
