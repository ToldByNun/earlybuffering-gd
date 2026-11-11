#ifndef EB_CHECKBOX_HPP
#define EB_CHECKBOX_HPP

// Settings-row checkbox: label left, rounded square right (image-2 chrome).
// Checked = accent fill + white check. Disabled = dimmed label + ghost box.
class EbCheckbox {
public:
    // Returns true when *value flipped. No-op when disabled or value is null.
    static bool draw(char const* id, char const* label, bool* value, bool enabled = true);

    // Reads/writes a bool SettingV3 key via EbDesktopMenuBind (no second store).
    static bool drawSetting(char const* key, char const* label, bool enabled = true);

private:
    EbCheckbox() = delete;
};

#endif
