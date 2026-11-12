#ifndef EB_COLOR_PICKER_HPP
#define EB_COLOR_PICKER_HPP

// Label + swatch row; click opens a popup with ImGui's classic hue-bar picker.
// Binds Geode color SettingV3 (ccColor3B) via EbDesktopMenuBind.
class EbColorPicker {
public:
    // rgb components in 0..1. Returns true when the color changes this frame.
    static bool draw(char const* id, char const* label, float rgb[3], bool enabled = true,
                     char const* tooltip = nullptr);

    // Binds a color SettingV3 key (e.g. save-icon-color).
    // Tooltip defaults to the setting's mod.json description.
    static bool drawSetting(char const* key, char const* label, bool enabled = true);

private:
    EbColorPicker() = delete;
};

#endif
