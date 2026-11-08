#include "dev_overlay.hpp"
#include "../../dev/dev_config.hpp"
#include "../../dev/dev_session.hpp"
#include "../../dev/reporting/dev_report.hpp"

bool DevOverlay::shouldShow() {
    if (!(!DevConfig::overlay())) {
        return DevSession::get().isRecording() || DevReport::hasLast();
    }
    return false;
}

std::string DevOverlay::line() {
    if (!shouldShow())
        return {};

    if (DevReport::hasLast()) {
        auto const& r = DevReport::last();
        // Prefer full report line (includes REC when set at eval time).
        // Refresh REC prefix if session is still rolling.
        if (DevSession::get().isRecording()) {
            DevReport copy = r;
            copy.recLine = DevSession::get().statusLine();
            return DevReport::formatOverlayLine(copy);
        }
        return r.overlayLine;
    }

    if (DevSession::get().isRecording()) {
        return DevSession::get().statusLine();
    }
    return {};
}
