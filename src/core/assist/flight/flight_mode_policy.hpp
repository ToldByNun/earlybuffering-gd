#ifndef FLIGHT_MODE_POLICY_HPP
#define FLIGHT_MODE_POLICY_HPP

#include "../../../config/stage_config.hpp"
#include <string>

// AP06: per-mode Straight Flight / Alternate contracts.
// Ordinary assist stays available everywhere; flight targeting is mode-gated.
class EbFlightModePolicy {
public:
    enum class FlightRole {
        OrdinaryOnly = 0, // Cube / Ball / UFO / Robot / Spider
        ShipLine = 1,     // Ship straight-flight targeting
        WavePattern = 2,  // Wave cadence — AP07 recognition, AP08 corrections
        SwingFlip = 3,    // Swing flip model (later) — measurement only for now
    };

    static FlightRole role(GameModeKind kind);
    static bool measuresHeading(GameModeKind kind);
    static bool allowsLineTargeting(GameModeKind kind);
    static bool allowsShipLineAssist(GameModeKind kind);
    static bool allowsWavePatternRecognition(GameModeKind kind);
    static bool allowsWavePatternAssist(GameModeKind kind);
    static bool allowsSwingFlipAssist(GameModeKind kind);
    static bool allowsSharedDualCorrection(GameModeKind kind);
    static char const* roleName(FlightRole role);
    static bool selfCheck(std::string& report);

private:
    EbFlightModePolicy() = delete;
};

#endif
