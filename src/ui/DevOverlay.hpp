#ifndef DEV_OVERLAY_HPP
#define DEV_OVERLAY_HPP

#include <string>

// Dev Lab HUD fragment for StatusOverlay.
// Appended when DevConfig::overlay() and (recording or last report).
class DevOverlay {
public:
    // One line: session REC + last report fields (fid/bestSurv/bestEval/obj/ideal).
    static std::string line();

    // True when Dev overlay content should be shown.
    static bool shouldShow();

private:
    DevOverlay() = delete;
};

#endif // DEV_OVERLAY_HPP
