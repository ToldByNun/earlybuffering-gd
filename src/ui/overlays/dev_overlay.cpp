#include "dev_overlay.hpp"
#include "../../dev/dev_config.hpp"
#include "../../dev/dev_session.hpp"
#include "../../dev/reporting/dev_report.hpp"

bool EbDevOverlay::shouldShow() {
    if (!(!EbDevConfig::overlay())) {
        return EbDevSession::get().isRecording() || EbDevReport::hasLast();
    }
    return false;
}

std::string EbDevOverlay::line() {
    if (!shouldShow())
        return {};

    if (EbDevReport::hasLast()) {
        auto const& r = EbDevReport::last();
        // Prefer full report line (includes REC when set at eval time).
        // Refresh REC prefix if session is still rolling.
        if (EbDevSession::get().isRecording()) {
            EbDevReport copy = r;
            copy.recLine = EbDevSession::get().statusLine();
            return EbDevReport::formatOverlayLine(copy);
        }
        return r.overlayLine;
    }

    if (EbDevSession::get().isRecording()) {
        return EbDevSession::get().statusLine();
    }
    return {};
}
