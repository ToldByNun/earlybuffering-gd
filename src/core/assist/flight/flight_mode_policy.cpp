#include "flight_mode_policy.hpp"

EbFlightModePolicy::FlightRole EbFlightModePolicy::role(GameModeKind kind) {
    switch (kind) {
    case GameModeKind::Ship:
        return FlightRole::ShipLine;
    case GameModeKind::Wave:
        return FlightRole::WavePattern;
    case GameModeKind::Swing:
        return FlightRole::SwingFlip;
    case GameModeKind::Cube:
    case GameModeKind::Ball:
    case GameModeKind::Ufo:
    case GameModeKind::Robot:
    case GameModeKind::Spider:
    case GameModeKind::Count:
        break;
    }
    return FlightRole::OrdinaryOnly;
}

bool EbFlightModePolicy::measuresHeading(GameModeKind kind) {
    auto const flightRole = role(kind);
    return flightRole == FlightRole::ShipLine || flightRole == FlightRole::WavePattern ||
           flightRole == FlightRole::SwingFlip;
}

bool EbFlightModePolicy::allowsLineTargeting(GameModeKind kind) {
    // Ship only until Wave/Swing have their own control models (AP07+).
    return role(kind) == FlightRole::ShipLine;
}

bool EbFlightModePolicy::allowsShipLineAssist(GameModeKind kind) {
    return allowsLineTargeting(kind);
}

bool EbFlightModePolicy::allowsWavePatternAssist(GameModeKind kind) {
    // AP07/AP08 arm Wave pattern corrections. AP06 only freezes the contract.
    (void)kind;
    return false;
}

bool EbFlightModePolicy::allowsSwingFlipAssist(GameModeKind kind) {
    (void)kind;
    return false;
}

bool EbFlightModePolicy::allowsSharedDualCorrection(GameModeKind kind) {
    // Dual must prove both affected paths before a shared correction. Solo only.
    (void)kind;
    return false;
}

char const* EbFlightModePolicy::roleName(FlightRole flightRole) {
    switch (flightRole) {
    case FlightRole::OrdinaryOnly:
        return "ordinary";
    case FlightRole::ShipLine:
        return "ship-line";
    case FlightRole::WavePattern:
        return "wave-pattern";
    case FlightRole::SwingFlip:
        return "swing-flip";
    }
    return "ordinary";
}

bool EbFlightModePolicy::selfCheck(std::string& report) {
    report.clear();
    int failures = 0;
    auto expect = [&](bool ok, char const* label) {
        if (ok)
            return;
        ++failures;
        if (!report.empty())
            report += "; ";
        report += label;
    };
    expect(role(GameModeKind::Cube) == FlightRole::OrdinaryOnly, "cube-ordinary");
    expect(role(GameModeKind::Ball) == FlightRole::OrdinaryOnly, "ball-ordinary");
    expect(role(GameModeKind::Ufo) == FlightRole::OrdinaryOnly, "ufo-ordinary");
    expect(role(GameModeKind::Robot) == FlightRole::OrdinaryOnly, "robot-ordinary");
    expect(role(GameModeKind::Spider) == FlightRole::OrdinaryOnly, "spider-ordinary");
    expect(role(GameModeKind::Ship) == FlightRole::ShipLine, "ship-line");
    expect(role(GameModeKind::Wave) == FlightRole::WavePattern, "wave-pattern");
    expect(role(GameModeKind::Swing) == FlightRole::SwingFlip, "swing-flip");
    expect(allowsLineTargeting(GameModeKind::Ship), "ship-line-on");
    expect(!allowsLineTargeting(GameModeKind::Wave), "wave-line-off");
    expect(!allowsLineTargeting(GameModeKind::Swing), "swing-line-off");
    expect(!allowsLineTargeting(GameModeKind::Cube), "cube-line-off");
    expect(measuresHeading(GameModeKind::Ship), "ship-measure");
    expect(measuresHeading(GameModeKind::Wave), "wave-measure");
    expect(measuresHeading(GameModeKind::Swing), "swing-measure");
    expect(!measuresHeading(GameModeKind::Cube), "cube-no-measure");
    expect(!allowsWavePatternAssist(GameModeKind::Wave), "wave-assist-deferred");
    expect(!allowsSwingFlipAssist(GameModeKind::Swing), "swing-assist-deferred");
    expect(!allowsSharedDualCorrection(GameModeKind::Ship), "dual-shared-off");
    expect(!allowsShipLineAssist(GameModeKind::Wave), "wave-no-ship-assist");
    return failures == 0;
}
