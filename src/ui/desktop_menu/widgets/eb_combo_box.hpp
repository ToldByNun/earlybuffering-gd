#ifndef EB_COMBO_BOX_HPP
#define EB_COMBO_BOX_HPP

// Compact inline combo for short one-of enums (label left, field right).
// Grey-tone chrome only — no sidebar wave (keeps short enums quiet).
class EbComboBox {
public:
    // options[i] are display + store values. Returns true when *selectedIndex changes.
    static bool draw(char const* id, char const* label, char const* const* options, int optionCount,
                     int* selectedIndex, bool enabled = true);

    // Binds a string SettingV3 key; options must match stored one-of values.
    static bool drawSetting(char const* key, char const* label, char const* const* options,
                            int optionCount, bool enabled = true);

private:
    EbComboBox() = delete;
};

#endif
