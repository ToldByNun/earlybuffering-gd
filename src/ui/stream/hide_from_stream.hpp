#ifndef HIDE_FROM_STREAM_HPP
#define HIDE_FROM_STREAM_HPP

#include <string>

// Windows: desktop ImGui + status HUD on a layered HWND with
// SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) so OBS/Game Capture
// see the game only; the player still sees EarlyBuffer chrome.
class EbHideFromStream {
public:
    static void install();
    static void syncFromSettings();

    static bool settingEnabled();
    static bool active();

    static void setStatusText(std::string text);
    static void setStatusVisible(bool visible);
    static std::string const& statusText();
    static bool statusVisible();

    // True while ImGui is recording into the capture-safe FBO (fix alpha blend).
    static bool captureSafeDrawActive();

private:
    EbHideFromStream() = delete;
};

#endif
