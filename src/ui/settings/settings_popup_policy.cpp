#include "settings_popup_policy.hpp"
#include <string>

bool EbSettingsPopupPolicy::ownsPopup(std::string_view title, std::string_view modName,
                                      bool hasOwnedSettings, bool hasForeignSettings) {
    if (modName.empty() || !hasOwnedSettings || hasForeignSettings)
        return false;
    // Geode's ModSettingsPopup title. A Keybinds popup may contain only our
    // rows after searching, so row ownership alone never identifies the host.
    return title == std::string("Settings for ") + std::string(modName);
}
