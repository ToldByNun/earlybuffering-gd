#ifndef EARLYBUFFER_CORE_MODEASSIST_HPP
#define EARLYBUFFER_CORE_MODEASSIST_HPP

#include "../config/StageConfig.hpp"

#include <string>

// Per-GameMode assist policy (parity-first tables). Hot path: forKind once per
// press → fill ModeAssistSnapshot POD; no virtuals inside cand loops.
struct ModeAssistSnapshot {
    GameModeKind kind = GameModeKind::Cube;

    int windowTicks = 4;
    float latencyWeight = 0.25f;
    // Pre-softened λ for spam-dense assists (Wave: base×0.65 when base>0.5).
    float latencyWeightSpam = 0.25f;

    int spamWindowTicks = 16;
    int spamMinCount = 4;
    float spamMaxMeanGap = 7.f;

    bool continuous = false;
    bool releaseAssistAlways = false; // wave/ship/swing/robot
    bool tapRelease = true;
    bool featheringIsControl = false; // ship/swing: feather ≠ PassSpam (C6)

    bool waveProximityPassAllowed = false;
    bool waveFutilePassAllowed = false;

    bool prefixWaveTurn = false;
    bool prefixShipRecover = false;
    bool prefixHoldBranch = false; // robot / continuous families

    int robustKFloor = 1;
    int robustKFloorSpam = 1; // waveLike spam bump floor

    float routeCorridorHalfY = 18.f; // Route mode (unused until Route ships)
    float foresightYReach = 90.f;
    float hazardYReach = 70.f;

    bool allowHus = true; // mirrors global husSoftHold for now
};

class ModeAssist {
public:
    virtual ~ModeAssist() = default;

    static ModeAssist const& forKind(GameModeKind kind);
    static ModeAssistSnapshot snapshotFor(
        GameModeKind kind,
        int windowTicks,
        bool husAllowed
    );

    virtual GameModeKind kind() const = 0;
    virtual char const* name() const = 0;

    virtual float baseLatencyWeight() const = 0;
    virtual float latencyWeight(bool spamDense) const;

    virtual int spamWindowTicks() const = 0;
    virtual int spamMinCount() const = 0;
    virtual float spamMaxMeanGap() const = 0;

    virtual bool continuous() const = 0;
    virtual bool releaseAssistAlways() const = 0;
    virtual bool tapRelease() const = 0;
    virtual bool featheringIsControl() const { return false; }

    virtual bool waveProximityPassAllowed() const { return false; }
    virtual bool waveFutilePassAllowed() const { return false; }

    virtual bool prefixWaveTurn() const { return false; }
    virtual bool prefixShipRecover() const { return false; }
    virtual bool prefixHoldBranch() const = 0;

    virtual int robustKFloor() const { return 1; }
    virtual int robustKFloorSpam() const { return 1; }

    virtual float routeCorridorHalfY() const = 0;
    virtual float foresightYReach() const = 0;
    virtual float hazardYReach() const = 0;

    // AssistHorizon short/long bases (limit = active window ticks).
    virtual void fillShortCaps(int limit, int& planCap, int& scoreHorizon, int& trajDraw) const = 0;
    virtual void fillLongCaps(int limit, int& planCap, int& scoreHorizon, int& trajDraw) const = 0;

    void fillSnapshot(ModeAssistSnapshot& out, int windowTicks, bool husAllowed) const;

    static bool selfCheck(std::string& report);
    static bool selfCheckKind(GameModeKind kind, std::string& report);

protected:
    ModeAssist() = default;
};

// --- Intermediate bases ---

class DiscreteJumpAssist : public ModeAssist {
public:
    bool continuous() const override { return false; }
    bool releaseAssistAlways() const override { return false; }
    bool tapRelease() const override { return true; }
    bool prefixHoldBranch() const override { return false; }
    int spamWindowTicks() const override { return 16; }
    int spamMinCount() const override { return 4; }
    float spamMaxMeanGap() const override { return 7.f; }
    float foresightYReach() const override { return 90.f; }
    float hazardYReach() const override { return 70.f; }
    void fillShortCaps(int limit, int& planCap, int& scoreHorizon, int& trajDraw) const override;
    void fillLongCaps(int limit, int& planCap, int& scoreHorizon, int& trajDraw) const override;
};

class FeatherContinuousAssist : public ModeAssist {
public:
    bool continuous() const override { return true; }
    bool releaseAssistAlways() const override { return true; }
    bool tapRelease() const override { return false; }
    bool featheringIsControl() const override { return true; }
    bool prefixShipRecover() const override { return true; }
    bool prefixHoldBranch() const override { return true; }
    int spamWindowTicks() const override { return 12; }
    int spamMinCount() const override { return 3; }
    float spamMaxMeanGap() const override { return 5.f; }
    int robustKFloor() const override { return 2; }
    int robustKFloorSpam() const override { return 2; }
    float foresightYReach() const override { return 100.f; }
    float hazardYReach() const override { return 100.f; }
    void fillShortCaps(int limit, int& planCap, int& scoreHorizon, int& trajDraw) const override;
    void fillLongCaps(int limit, int& planCap, int& scoreHorizon, int& trajDraw) const override;
};

class TapContinuousAssist : public ModeAssist {
public:
    bool continuous() const override { return true; }
    bool releaseAssistAlways() const override { return false; }
    bool tapRelease() const override { return true; }
    bool prefixShipRecover() const override { return true; }
    bool prefixHoldBranch() const override { return true; }
    int spamWindowTicks() const override { return 14; }
    int spamMinCount() const override { return 4; }
    float spamMaxMeanGap() const override { return 6.f; }
    float foresightYReach() const override { return 90.f; }
    float hazardYReach() const override { return 70.f; }
    void fillShortCaps(int limit, int& planCap, int& scoreHorizon, int& trajDraw) const override;
    void fillLongCaps(int limit, int& planCap, int& scoreHorizon, int& trajDraw) const override;
};

// --- Leaves ---

class CubeModeAssist : public DiscreteJumpAssist {
public:
    GameModeKind kind() const override { return GameModeKind::Cube; }
    char const* name() const override { return "cube"; }
    float baseLatencyWeight() const override { return 0.25f; }
    float routeCorridorHalfY() const override { return 18.f; }
};

class BallModeAssist : public DiscreteJumpAssist {
public:
    GameModeKind kind() const override { return GameModeKind::Ball; }
    char const* name() const override { return "ball"; }
    float baseLatencyWeight() const override { return 0.35f; }
    float routeCorridorHalfY() const override { return 16.f; }
};

class RobotModeAssist : public ModeAssist {
public:
    GameModeKind kind() const override { return GameModeKind::Robot; }
    char const* name() const override { return "robot"; }
    float baseLatencyWeight() const override { return 0.40f; }
    bool continuous() const override { return false; }
    bool releaseAssistAlways() const override { return true; }
    bool tapRelease() const override { return false; }
    bool prefixHoldBranch() const override { return true; }
    int spamWindowTicks() const override { return 16; }
    int spamMinCount() const override { return 4; }
    float spamMaxMeanGap() const override { return 7.f; }
    float routeCorridorHalfY() const override { return 18.f; }
    float foresightYReach() const override { return 90.f; }
    float hazardYReach() const override { return 70.f; }
    void fillShortCaps(int limit, int& planCap, int& scoreHorizon, int& trajDraw) const override;
    void fillLongCaps(int limit, int& planCap, int& scoreHorizon, int& trajDraw) const override;
};

class ShipModeAssist : public FeatherContinuousAssist {
public:
    GameModeKind kind() const override { return GameModeKind::Ship; }
    char const* name() const override { return "ship"; }
    float baseLatencyWeight() const override { return 0.75f; }
    float routeCorridorHalfY() const override { return 14.f; }
};

class SwingModeAssist : public FeatherContinuousAssist {
public:
    GameModeKind kind() const override { return GameModeKind::Swing; }
    char const* name() const override { return "swing"; }
    float baseLatencyWeight() const override { return 0.85f; }
    float routeCorridorHalfY() const override { return 14.f; }
};

class UfoModeAssist : public TapContinuousAssist {
public:
    GameModeKind kind() const override { return GameModeKind::Ufo; }
    char const* name() const override { return "ufo"; }
    float baseLatencyWeight() const override { return 0.35f; }
    float routeCorridorHalfY() const override { return 16.f; }
};

class SpiderModeAssist : public TapContinuousAssist {
public:
    GameModeKind kind() const override { return GameModeKind::Spider; }
    char const* name() const override { return "spider"; }
    float baseLatencyWeight() const override { return 0.40f; }
    float routeCorridorHalfY() const override { return 18.f; }
};

class WaveModeAssist : public ModeAssist {
public:
    GameModeKind kind() const override { return GameModeKind::Wave; }
    char const* name() const override { return "wave"; }
    float baseLatencyWeight() const override { return 1.00f; }
    bool continuous() const override { return true; }
    bool releaseAssistAlways() const override { return true; }
    bool tapRelease() const override { return false; }
    bool waveProximityPassAllowed() const override { return true; }
    bool waveFutilePassAllowed() const override { return true; }
    bool prefixWaveTurn() const override { return true; }
    bool prefixHoldBranch() const override { return true; }
    int spamWindowTicks() const override { return 10; }
    int spamMinCount() const override { return 3; }
    float spamMaxMeanGap() const override { return 4.f; }
    int robustKFloor() const override { return 2; }
    int robustKFloorSpam() const override { return 2; }
    float routeCorridorHalfY() const override { return 10.f; }
    float foresightYReach() const override { return 100.f; }
    float hazardYReach() const override { return 100.f; }
    void fillShortCaps(int limit, int& planCap, int& scoreHorizon, int& trajDraw) const override;
    void fillLongCaps(int limit, int& planCap, int& scoreHorizon, int& trajDraw) const override;
};

#endif // EARLYBUFFER_CORE_MODEASSIST_HPP
