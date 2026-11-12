#ifndef EB_BUTTON_HPP
#define EB_BUTTON_HPP

// Action button chrome for the desktop ImGui settings shell.
// Full-width by default; beginRow/endRow splits equal-width columns with a gap.
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

    // Equal-width multi-button row. Call beginRow(n), then exactly n draw/drawAction,
    // then endRow(). Nested rows are ignored (inner begin is a no-op).
    // gapPx < 0 → use style ItemSpacing.x so H-gap matches V-gap between rows.
    static void beginRow(int buttonCount, float gapPx = -1.0f);
    static void endRow();

private:
    EbButton() = delete;
};

#endif
