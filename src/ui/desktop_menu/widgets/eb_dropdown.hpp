#ifndef EB_DROPDOWN_HPP
#define EB_DROPDOWN_HPP

// Full-width popup dropdown for longer option lists (label above, field below).
// Grey-tone chrome only — same SettingV3 string binding as EbComboBox.
class EbDropdown {
public:
    // options[i] are store values. displayLabels[i] optional UI text (nullptr → options).
    // Returns true when *selectedIndex changes.
    static bool draw(char const* id, char const* label, char const* const* options, int optionCount,
                     int* selectedIndex, char const* const* displayLabels = nullptr,
                     bool enabled = true);

    // Binds a string SettingV3 key; options must match stored one-of values.
    static bool drawSetting(char const* key, char const* label, char const* const* options,
                            int optionCount, char const* const* displayLabels = nullptr,
                            bool enabled = true);

private:
    EbDropdown() = delete;
};

#endif
