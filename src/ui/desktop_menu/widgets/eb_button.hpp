#ifndef EB_BUTTON_HPP
#define EB_BUTTON_HPP

// Pill / full-row button chrome for the desktop ImGui settings shell.
// Primary uses the sidebar wave fill. Actions route through eb-action bridges.
class EbButton {
public:
    enum class Style { Primary, Row };

    // Returns true on click. Primary = wave pill; Row = full-width surface.
    static bool draw(char const* id, char const* label, bool enabled = true,
                     Style style = Style::Primary);

    // Fires EbActionButtonSettingImplementation::fireSettingAction(actionKey).
    // Label falls back through dynamicButtonLabel; enable via actionShouldEnable.
    static bool drawAction(char const* actionKey, char const* fallbackLabel,
                           Style style = Style::Primary);

private:
    EbButton() = delete;
};

#endif
