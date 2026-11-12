#ifndef DESKTOP_MENU_TOOLTIP_HPP
#define DESKTOP_MENU_TOOLTIP_HPP

#include <string>

// Hover tooltips for desktop ImGui widgets (setting descriptions + smoke hints).
class EbDesktopMenuTooltip {
public:
    // mod.json "description" for a SettingV3 key, or empty if missing.
    static std::string descriptionForKey(char const* key);

    // Call right after the widget's interactive item. No-op when text is empty
    // or the last item is not hovered (uses ImGui delay so it doesn't flash).
    static void show(char const* text);

private:
    EbDesktopMenuTooltip() = delete;
};

#endif
