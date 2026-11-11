#ifndef EB_BUTTON_HPP
#define EB_BUTTON_HPP

// Full-width row button chrome for the desktop ImGui settings shell.
// Actions route through eb-action bridges.
class EbButton {
public:
    // Returns true on click.
    static bool draw(char const* id, char const* label, bool enabled = true,
                     char const* tooltip = nullptr);

    // Fires EbActionButtonSettingImplementation::fireSettingAction(actionKey).
    // Label falls back through dynamicButtonLabel; enable via actionShouldEnable.
    // Tooltip defaults to the action setting's mod.json description.
    static bool drawAction(char const* actionKey, char const* fallbackLabel);

private:
    EbButton() = delete;
};

#endif
