#include "shadow_regression_checks.hpp"
#include "../mode_survival.hpp"
#include "../planning/shadow_survival_rules.hpp"
#include "../planning/simulation_plan.hpp"
#include "../shadow_simulator.hpp"
#include <string>

bool ShadowRegressionChecks::regressionSelfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    auto binOf = [](int ticks) {
        if (ticks < 24)
            return 0;
        if (ticks < 48)
            return 1;
        if (ticks < 96)
            return 2;
        return 3;
    };
    auto clears = [](ShadowSimulator::RunResult const& r, ThreatZone const& zone) {
        if (!zone.valid)
            return !r.died || r.unknownTrunc;
        if (!r.died)
            return r.endX > zone.endX || r.zoneCleared;
        return r.deathX > zone.endX;
    };

    // Attempt-start bake slot must stay out of the rotating ring.
    expect(ShadowSimulator::START_SNAPSHOT_SLOT == 0, "start-slot-0");
    expect(ShadowSimulator::SNAPSHOT_RING_BEGIN == 1, "ring-begins-1");
    expect(ShadowSimulator::SNAPSHOT_RING_BEGIN < ShadowSimulator::SNAPSHOT_RING_END,
           "ring-lt-end");
    expect(ShadowSimulator::SNAPSHOT_RING_END == ShadowSimulator::PARITY_SLOT_A,
           "parity-after-ring");
    expect(ShadowSimulator::PARITY_SLOT_B == ShadowSimulator::SNAPSHOT_SLOTS - 1, "parity-b-last");
    expect(ShadowSimulator::SNAPSHOT_RING_END < ShadowSimulator::SNAPSHOT_SLOTS,
           "ring-end-lt-slots");

    // Fixed survive bins [0,24), [24,48), [48,96), [96,∞)
    expect(binOf(0) == 0, "bin0");
    expect(binOf(23) == 0, "bin23");
    expect(binOf(24) == 1, "bin24");
    expect(binOf(47) == 1, "bin47");
    expect(binOf(48) == 2, "bin48");
    expect(binOf(95) == 2, "bin95");
    expect(binOf(96) == 3, "bin96");

    ThreatZone zone;
    zone.valid = true;
    zone.startX = 100.f;
    zone.endX = 200.f;

    ShadowSimulator::RunResult inZone;
    inZone.died = true;
    inZone.deathX = 180.f;
    expect(!clears(inZone, zone), "spike-row-not-clear");

    ShadowSimulator::RunResult pastZone;
    pastZone.died = true;
    pastZone.deathX = 201.f;
    expect(clears(pastZone, zone), "past-zone-clear");

    ShadowSimulator::RunResult survivePast;
    survivePast.died = false;
    survivePast.endX = 250.f;
    expect(clears(survivePast, zone), "survive-past-clear");

    ShadowSimulator::RunResult surviveInside;
    surviveInside.died = false;
    surviveInside.endX = 150.f;
    expect(!clears(surviveInside, zone), "survive-inside-not-clear");

    ShadowSimulator::RunResult unk;
    unk.unknownTrunc = true;
    unk.died = false;
    unk.endX = 250.f;
    expect(clears(unk, zone), "unknown-after-clear");

    expect(ShadowSimulator::kClusterGapX == 50.f, "clusterGapX");
    expect(ShadowSimulator::kWindowMarginK == 1, "windowMarginK");
    expect(ShadowSimulator::kDefaultTickBudget == 4000, "tickBudget");
    expect(ShadowSimulator::kFutileEndXGain == 8.f, "futileEndXGain");

    // Wave proximity / futile-delay gates (+ foresight orb/pad must not Pass)
    expect(ShadowSimulator::shouldWaveHazardProximityPass(true, true, true, false, true),
           "wave-prox-pass");
    expect(!ShadowSimulator::shouldWaveHazardProximityPass(true, true, true, false, false),
           "wave-prox-release-never");
    expect(!ShadowSimulator::shouldWaveHazardProximityPass(true, true, false, false, true),
           "wave-prox-orb-target-blocks");
    expect(!ShadowSimulator::shouldWaveHazardProximityPass(true, true, true, true, true),
           "wave-prox-foresight-blocks");
    expect(!ShadowSimulator::shouldWaveHazardProximityPass(false, true, true, false, true),
           "wave-prox-not-cube");
    expect(!ShadowSimulator::shouldWaveHazardProximityPass(true, false, true, false, true),
           "wave-prox-no-hazard");
    expect(ShadowSimulator::shouldWaveFutileDelayPass(true, false, 0, 3.f, false, true),
           "wave-futile-pass");
    expect(!ShadowSimulator::shouldWaveFutileDelayPass(true, false, 0, 3.f, false, false),
           "wave-futile-release-never");
    expect(!ShadowSimulator::shouldWaveFutileDelayPass(true, true, 0, 3.f, false, true),
           "wave-futile-orb-intent");
    expect(!ShadowSimulator::shouldWaveFutileDelayPass(true, false, 0, 3.f, true, true),
           "wave-futile-foresight");
    expect(!ShadowSimulator::shouldWaveFutileDelayPass(true, false, 1, 3.f, false, true),
           "wave-futile-bin");
    expect(!ShadowSimulator::shouldWaveFutileDelayPass(true, false, 0, 12.f, false, true),
           "wave-futile-gain");

    // Continuous idleSafer / ascentUseful
    expect(ShadowSimulator::classifyIdleSafer(true, true, false, -1, 0.f, true, 4, 100.f),
           "idle-safer-alive");
    expect(!ShadowSimulator::classifyIdleSafer(false, true, false, -1, 0.f, true, 4, 100.f),
           "idle-safer-not-cube");
    expect(!ShadowSimulator::classifyIdleSafer(true, true, false, -1, 0.f, false, -1, 0.f),
           "idle-safer-d0-safe");
    expect(ShadowSimulator::classifyIdleSafer(true, true, true, 10, 200.f, true, 3, 100.f),
           "idle-safer-later-death");
    expect(ShadowSimulator::deathLooksLikeAscent(100.f, 140.f, false), "ascent-death-above");
    expect(!ShadowSimulator::deathLooksLikeAscent(100.f, 105.f, false), "ascent-death-near");
    expect(ShadowSimulator::deathLooksLikeAscent(100.f, 60.f, true), "ascent-death-flipped");
    expect(ShadowSimulator::classifyAscentUseful(true, true, false, 0, 50.f, true, 0, 50.f, true),
           "ascent-later-viable");
    expect(ShadowSimulator::classifyAscentUseful(true, true, true, 0, 50.f, true, 2, 50.f, true),
           "ascent-better-bin");
    expect(!ShadowSimulator::classifyAscentUseful(true, true, true, 1, 50.f, true, 1, 52.f, true),
           "ascent-no-gain");
    // Dead soft / high bin without Alive later must not invent ascentUseful.
    expect(
        !ShadowSimulator::classifyAscentUseful(true, true, false, 0, 50.f, false, 3, 90.f, false),
        "ascent-dead-not-useful");
    expect(!ShadowSimulator::classifyAscentUseful(true, true, false, 0, 50.f, true, 3, 90.f, false),
           "ascent-soft-died-not-useful");
    expect(!ShadowSimulator::classifyAscentUseful(true, true, true, 0, 50.f, true, 2, 50.f, false),
           "ascent-alive-flag-required");

    expect(ShadowSimulator::pickPreferredRelease(2, 10, true) == 2, "pref-rel-cont");
    expect(ShadowSimulator::pickPreferredRelease(2, 10, false) == 6, "pref-rel-tap");
    expect(ShadowSimulator::pickPreferredRelease(-1, 5, false) < 0, "pref-rel-unset");
    expect(ShadowSimulator::pickPreferredRelease(4, 4, false) == 4, "pref-rel-same");

    expect(ShadowSimulator::autoRobustKHints(true, true, 1.f, 0.f, false) == 2, "k-wave");
    expect(ShadowSimulator::autoRobustKHints(true, true, 1.2f, 0.f, true) == 3, "k-wave-spam-spd");
    expect(ShadowSimulator::autoRobustKHints(true, true, 1.f, 0.f, true) == 2, "k-wave-spam-slow");
    expect(ShadowSimulator::autoRobustKHints(false, false, 1.f, 0.f, false) == 1, "k-cube");
    expect(ShadowSimulator::autoRobustKHints(false, false, 1.6f, 0.f, false) == 3, "k-fast");
    expect(ShadowSimulator::autoRobustKHints(true, false, 1.f, 0.1f, false) == 3, "k-dt-bump");

    // Synthetic early-click snaps: d=0 dies in zone → Delay must clear past zoneEndX.
    class Snap {
    public:
        float deathX;
        int clearDelay;

    private:
    };
    Snap snaps[] = {
        {150.f, 4}, {120.f, 3}, {190.f, 6}, {105.f, 2}, {175.f, 5},
    };
    for (auto const& s : snaps) {
        expect(s.clearDelay > 0 && s.deathX <= zone.endX, "snap-needs-delay");
        ShadowSimulator::RunResult d0;
        d0.died = true;
        d0.deathX = s.deathX;
        expect(!clears(d0, zone), "snap-d0-unsafe");
        ShadowSimulator::RunResult dn;
        dn.died = false;
        dn.endX = zone.endX + 40.f;
        expect(clears(dn, zone), "snap-delay-clears");
    }

    // Score honesty: soft-viable surviveBin Escape is gone.
    expect(!ShadowSurvivalRules::delayViableHonest(false, false, false, false, false, false, true),
           "soft-viable-no-clear");
    expect(ShadowSurvivalRules::delayViableHonest(false, false, false, false, true, false, true),
           "viable-clears-threat");
    expect(ShadowSurvivalRules::delayViableHonest(false, false, false, false, false, true, true),
           "viable-zone-cleared");
    expect(ShadowSurvivalRules::delayViableHonest(false, false, false, false, false, false, false),
           "viable-no-zone");
    // Soft bake: raw death + Landing/LateCrash can be not-false-lethal; Lethal cannot.
    expect(!ShadowSurvivalRules::delayViableHonest(false, true, false, false, true, false, true,
                                                   ModeSurvival::Class::Lethal),
           "lethal-died-not-viable");
    expect(ShadowSurvivalRules::delayViableHonest(false, true, false, false, true, true, true,
                                                  ModeSurvival::Class::Landing),
           "landing-died-soft-viable");
    expect(ShadowSurvivalRules::delayViableHonest(false, true, false, false, true, true, true,
                                                  ModeSurvival::Class::LateCrashAfterClear),
           "late-crash-soft-viable");
    // Live Delay-worthy: Alive only; Landing/LateCrash never invent Delay.
    expect(ShadowSurvivalRules::delayViableHonest(false, false, false, false, true, false, true,
                                                  ModeSurvival::Class::Alive, true),
           "alive-require-worthy");
    expect(!ShadowSurvivalRules::delayViableHonest(false, true, false, false, true, true, true,
                                                   ModeSurvival::Class::Landing, true),
           "landing-not-delay-worthy");
    expect(!ShadowSurvivalRules::delayViableHonest(false, true, false, false, true, true, true,
                                                   ModeSurvival::Class::LateCrashAfterClear, true),
           "late-crash-not-delay-worthy");
    expect(!ShadowSurvivalRules::delayViableHonest(false, true, false, false, true, true, true,
                                                   ModeSurvival::Class::Recoverable, true),
           "recoverable-not-delay-worthy");
    expect(!ShadowSurvivalRules::delayViableHonest(false, false, true, false, true, false, true),
           "unknown-not-viable");
    expect(!ShadowSurvivalRules::delayViableHonest(false, false, false, true, true, false, true),
           "toggle-unk-not-viable");
    expect(ShadowSurvivalRules::exitSurvivedHonest(false, false), "exit-alive");
    expect(!ShadowSurvivalRules::exitSurvivedHonest(true, false), "exit-died");
    expect(!ShadowSurvivalRules::exitSurvivedHonest(false, true), "exit-unknown-not-survive");
    expect(!ShadowSurvivalRules::exitSurvivedHonest(true, true), "exit-died-unk");

    if (fails == 0)
        report = "regressionSelfCheck OK (bins+zone+5 snaps+score-honesty)";
    else
        report = fmt::format("regressionSelfCheck {} fails: {}", fails, report);
    return fails == 0;
}
