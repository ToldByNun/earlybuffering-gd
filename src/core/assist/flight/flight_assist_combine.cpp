#include "flight_assist_combine.hpp"
#include "../contracts/selected_tick_guard.hpp"
#include <cmath>
#include <string>

EbFlightAssistCombine::Lane EbFlightAssistCombine::selectLane(EdgeContext const& context) {
    if (!context.lifecycleOk)
        return Lane::Ordinary;
    // 1) Explicit direction change always wins — even over a verified rescue.
    if (context.directionChange)
        return Lane::DirectionChange;
    // 2) Imminent collision on the requested edge → ordinary verified rescue.
    if (context.baselineDied)
        return Lane::CollisionRescue;
    // Dash holds and orb timing share the same final selection as flight lanes:
    // never arm Ship/Wave preference while a dash is held, and never let rhythm
    // preference outrank an active orb activation window when both are eligible.
    if (context.dashHold)
        return Lane::Ordinary;
    if (context.orbTiming && !context.shipLineArmed && !context.wavePatternArmed)
        return Lane::Ordinary;
    // 3) Acquired Ship line (straight flight).
    if (context.shipLineArmed && context.shipTracking && context.shipConfidence >= .15)
        return Lane::FlightLine;
    // 4) Locked Wave hold/release motif (alternate rhythm).
    if (context.wavePatternArmed &&
        preferAlternateOverOrdinary(context.waveTracking, context.waveConfidence))
        return Lane::AlternateRhythm;
    // 5) Ordinary assist / pass-through.
    return Lane::Ordinary;
}

EbFlightAssistCombine::Lane EbFlightAssistCombine::selectLaneDeterministic(
    EdgeContext const& context, int budgetRemainingUs) {
    // Exhausted budget cannot invent a flight/rhythm correction.
    if (budgetRemainingUs <= 0 && !context.directionChange && !context.baselineDied)
        return Lane::Ordinary;
    return selectLane(context);
}

char const* EbFlightAssistCombine::laneName(Lane lane) {
    switch (lane) {
    case Lane::DirectionChange:
        return "DirectionChange";
    case Lane::CollisionRescue:
        return "CollisionRescue";
    case Lane::FlightLine:
        return "FlightLine";
    case Lane::AlternateRhythm:
        return "AlternateRhythm";
    case Lane::Ordinary:
        return "Ordinary";
    }
    return "Ordinary";
}

bool EbFlightAssistCombine::shouldCancelPending(bool yieldRequested, bool pendingOwned) {
    return yieldRequested && pendingOwned;
}

bool EbFlightAssistCombine::resetClearsPending(bool learnedReset, bool flightOwnedPending) {
    return learnedReset && flightOwnedPending;
}

bool EbFlightAssistCombine::preferAlternateOverOrdinary(bool waveTracking, double confidence) {
    return waveTracking && confidence >= .2;
}

int EbFlightAssistCombine::guardFlightDelay(int proposed, int planned, std::span<int const> verified,
                                          bool budgetIncomplete) {
    if (budgetIncomplete)
        return planned > 0 ? planned : 0;
    return EbSelectedTickGuard::keepVerifiedLearningDelay(proposed, planned, verified);
}

bool EbFlightAssistCombine::CacheKey::reuseAllowed(CacheKey const& stored, CacheKey const& live) {
    if (stored.effectTick < 0 || live.effectTick < 0)
        return false;
    if (stored.worldStamp != live.worldStamp || stored.playerStamp != live.playerStamp)
        return false;
    if (stored.effectTick != live.effectTick || stored.edgeSequence != live.edgeSequence)
        return false;
    if (stored.phaseMillis != live.phaseMillis || stored.settingsStamp != live.settingsStamp)
        return false;
    if (stored.portalEpoch != live.portalEpoch)
        return false;
    // Timewarp / mover epoch baked into stamps; also reject float drift.
    if (std::abs(stored.timeWarp - live.timeWarp) > 1e-4f)
        return false;
    return true;
}

EbFlightAssistCombine::CacheKey EbFlightAssistCombine::CacheKey::invalidate() {
    return {};
}

bool EbFlightAssistCombine::selfCheck(std::string& report) {
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

    // Acceptance: straight flight → deliberate climb → new angled alternating pattern.
    EdgeContext shipSteady;
    shipSteady.shipLineArmed = true;
    shipSteady.shipTracking = true;
    shipSteady.shipConfidence = .7;
    expect(selectLane(shipSteady) == Lane::FlightLine, "intent-ship-line");

    EdgeContext shipClimb = shipSteady;
    shipClimb.directionChange = true;
    expect(selectLane(shipClimb) == Lane::DirectionChange, "intent-ship-climb");
    expect(shouldCancelPending(true, true), "cancel-ship-pending");

    EdgeContext waveAlt;
    waveAlt.wavePatternArmed = true;
    waveAlt.waveTracking = true;
    waveAlt.waveConfidence = .55;
    expect(selectLane(waveAlt) == Lane::AlternateRhythm, "intent-wave-alt");
    expect(preferAlternateOverOrdinary(true, .55), "motif-prefer");
    expect(!preferAlternateOverOrdinary(true, .1), "motif-low-conf");

    EdgeContext waveYield = waveAlt;
    waveYield.directionChange = true;
    expect(selectLane(waveYield) == Lane::DirectionChange, "intent-wave-yield");

    // Collision rescue outranks flight/rhythm preference.
    EdgeContext rescue = shipSteady;
    rescue.baselineDied = true;
    expect(selectLane(rescue) == Lane::CollisionRescue, "rescue-over-ship");
    EdgeContext rescueWave = waveAlt;
    rescueWave.baselineDied = true;
    expect(selectLane(rescueWave) == Lane::CollisionRescue, "rescue-over-wave");

    // Direction change still beats rescue.
    EdgeContext climbThreat = rescue;
    climbThreat.directionChange = true;
    expect(selectLane(climbThreat) == Lane::DirectionChange, "climb-over-rescue");

    // Dash / orb share one final selection with ordinary when flight is inactive.
    EdgeContext dash;
    dash.dashHold = true;
    dash.shipLineArmed = true;
    dash.shipTracking = true;
    dash.shipConfidence = .9;
    expect(selectLane(dash) == Lane::Ordinary, "dash-blocks-ship");

    EdgeContext orbOnly;
    orbOnly.orbTiming = true;
    expect(selectLane(orbOnly) == Lane::Ordinary, "orb-ordinary");

    // Cache reuse rejects portal / timewarp / new edge.
    CacheKey a;
    a.worldStamp = 1;
    a.playerStamp = 2;
    a.effectTick = 10;
    a.edgeSequence = 3;
    a.phaseMillis = 40;
    a.settingsStamp = 7;
    a.timeWarp = 1.f;
    CacheKey b = a;
    expect(CacheKey::reuseAllowed(a, b), "cache-hit");
    b.timeWarp = 1.5f;
    expect(!CacheKey::reuseAllowed(a, b), "cache-timewarp");
    b = a;
    b.portalEpoch = true;
    expect(!CacheKey::reuseAllowed(a, b), "cache-portal");
    b = a;
    b.edgeSequence = 4;
    expect(!CacheKey::reuseAllowed(a, b), "cache-new-edge");
    expect(!CacheKey::reuseAllowed(CacheKey::invalidate(), a), "cache-invalidate");

    // Selected-tick guard: unverified learned delay cannot replace planned.
    int verified[] = {2, 3, 4};
    expect(guardFlightDelay(5, 3, verified, false) == 3, "guard-keep-planned");
    expect(guardFlightDelay(3, 3, verified, false) == 3, "guard-keep-same");
    expect(guardFlightDelay(4, 3, verified, false) == 4, "guard-verified-ok");
    expect(guardFlightDelay(4, 3, verified, true) == 3, "guard-budget-incomplete");

    expect(resetClearsPending(true, true), "reset-clears");
    expect(!resetClearsPending(false, true), "reset-only-on-reset");

    // Determinism under equal budgets.
    expect(selectLaneDeterministic(shipSteady, 1000) == selectLaneDeterministic(shipSteady, 1000),
           "det-same");
    expect(selectLaneDeterministic(shipSteady, 0) == Lane::Ordinary, "det-budget-zero");
    expect(selectLaneDeterministic(shipClimb, 0) == Lane::DirectionChange, "det-climb-budget0");

    expect(laneName(Lane::FlightLine)[0] != '\0', "lane-name");

    if (failures == 0) {
        report = "flightAssistCombine OK";
        return true;
    }
    report = "flightAssistCombine " + std::to_string(failures) + " fails: " + report;
    return false;
}
