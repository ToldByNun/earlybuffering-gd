#ifndef EB_ELEMENT_LIST_HPP
#define EB_ELEMENT_LIST_HPP

// CS-style add/remove list chrome for the desktop ImGui settings shell.
// Assist Off Ranges binds through EbStageConfig (assist-off-ranges JSON), not 12 sliders.
class EbElementList {
public:
    // Draw the Assist Off Ranges editor. Returns true when the stored ranges changed.
    static bool drawAssistOffRanges();

private:
    EbElementList() = delete;
};

#endif
