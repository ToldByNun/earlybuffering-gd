#include "test_settings_popup_policy.hpp"
#include "../src/ui/settings/settings_popup_policy.hpp"
#include <iostream>

int SettingsPopupRegression::run() {
    int failures = 0;
    auto expect = [&](bool passed, char const* name) {
        if (passed)
            return;
        ++failures;
        std::cerr << name << '\n';
    };
    expect(EbSettingsPopupPolicy::ownsPopup("Settings for EarlyBuffer", "EarlyBuffer", true, false),
           "recognize actual mod settings");
    // Reproduces global Keybinds, including a search filtered to our own bindings.
    expect(!EbSettingsPopupPolicy::ownsPopup("Keybinds", "EarlyBuffer", true, true),
           "global keybind list must stay intact");
    expect(!EbSettingsPopupPolicy::ownsPopup("Keybinds", "EarlyBuffer", true, false),
           "own keybinds do not establish popup ownership");
    expect(
        !EbSettingsPopupPolicy::ownsPopup("Settings for Another Mod", "EarlyBuffer", true, false),
        "foreign popup containing an own row");
    expect(!EbSettingsPopupPolicy::ownsPopup("Settings for EarlyBuffer", "EarlyBuffer", true, true),
           "never filter a mixed-mod list");
    expect(
        !EbSettingsPopupPolicy::ownsPopup("Settings for EarlyBuffer", "EarlyBuffer", false, false),
        "title text alone is not sufficient");
    expect(
        !EbSettingsPopupPolicy::ownsPopup("Open EarlyBuffer Settings", "EarlyBuffer", true, false),
        "settings button labels are not popup titles");
    expect(EbSettingsPopupPolicy::ownsPopup("Settings for Renamed Mod", "Renamed Mod", true, false),
           "use metadata mod name");
    std::cout << "Settings popup ownership regression: " << failures << " failures\n";
    return failures ? 1 : 0;
}

int main() {
    return SettingsPopupRegression::run();
}
