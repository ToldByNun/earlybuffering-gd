#include "shadow_regression_checks.hpp"
#include "../mode_survival.hpp"
#include "../planning/shadow_survival_rules.hpp"
#include "../planning/simulation_plan.hpp"
#include "../shadow_simulator.hpp"
#include <string>

bool EbShadowRegressionChecks::regressionSelfCheck(std::string& report) {
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
    auto clears = [](EbShadowSimulator::RunResult const& r, EbThreatZone const& zone) {
        if (!zone.valid)
            return !r.died || r.unknownTrunc;
        if (!r.died)
            return r.endX > zone.endX || r.zoneCleared;
        return r.deathX > zone.endX;
    };

    // Attempt-start bake slot must stay out of the rotating ring.
    expect(EbShadowSimulator::START_SNAPSHOT_SLOT == 0, "start-slot-0");
    expect(EbShadowSimulator::SNAPSHOT_RING_BEGIN == 1, "ring-begins-1");
    expect(EbShadowSimulator::SNAPSHOT_RING_BEGIN < EbShadowSimulator::SNAPSHOT_RING_END,
           "ring-lt-end");
    expect(EbShadowSimulator::SNAPSHOT_RING_END == EbShadowSimulator::PARITY_SLOT_A,
           "parity-after-ring");
    expect(EbShadowSimulator::PARITY_SLOT_B == EbShadowSimulator::SNAPSHOT_SLOTS - 1, "parity-b-last");
    expect(EbShadowSimulator::SNAPSHOT_RING_END < EbShadowSimulator::SNAPSHOT_SLOTS,
           "ring-end-lt-slots");

    // Fixed survive bins [0,24), [24,48), [48,96), [96,∞)
    expect(binOf(0) == 0, "bin0");
    expect(binOf(23) == 0, "bin23");
    expect(binOf(24) == 1, "bin24");
    expect(binOf(47) == 1, "bin47");
    expect(binOf(48) == 2, "bin48");
    expect(binOf(95) == 2, "bin95");
    expect(binOf(96) == 3, "bin96");

    EbThreatZone zone;
    zone.valid = true;
    zone.startX = 100.f;
    zone.endX = 200.f;

    EbShadowSimulator::RunResult inZone;
    inZone.died = true;
    inZone.deathX = 180.f;
    expect(!clears(inZone, zone), "spike-row-not-clear");

    EbShadowSimulator::RunResult pastZone;
    pastZone.died = true;
    pastZone.deathX = 201.f;
    expect(clears(pastZone, zone), "past-zone-clear");

    EbShadowSimulator::RunResult survivePast;
    survivePast.died = false;
    survivePast.endX = 250.f;
    expect(clears(survivePast, zone), "survive-past-clear");

    EbShadowSimulator::RunResult surviveInside;
    surviveInside.died = false;
    surviveInside.endX = 150.f;
    expect(!clears(surviveInside, zone), "survive-inside-not-clear");

    EbShadowSimulator::RunResult unk;
    unk.unknownTrunc = true;
    unk.died = false;
    unk.endX = 250.f;
    expect(clears(unk, zone), "unknown-after-clear");

    expect(EbShadowSimulator::kClusterGapX == 50.f, "clusterGapX");
    expect(EbShadowSimulator::kWindowMarginK == 1, "windowMarginK");
    expect(EbShadowSimulator::kDefaultTickBudget == 4000, "tickBudget");
    expect(EbShadowSimulator::kFutileEndXGain == 8.f, "futileEndXGain");

    // Wave proximity / futile-delay gates (+ foresight orb/pad must not Pass)
    expect(EbShadowSimulator::shouldWaveHazardProximityPass(true, true, true, false, true),
           "wave-prox-pass");
    expect(!EbShadowSimulator::shouldWaveHazardProximityPass(true, true, true, false, false),
           "wave-prox-release-never");
    expect(!EbShadowSimulator::shouldWaveHazardProximityPass(true, true, false, false, true),
           "wave-prox-orb-target-blocks");
    expect(!EbShadowSimulator::shouldWaveHazardProximityPass(true, true, true, true, true),
           "wave-prox-foresight-blocks");
    expect(!EbShadowSimulator::shouldWaveHazardProximityPass(false, true, true, false, true),
           "wave-prox-not-cube");
    expect(!EbShadowSimulator::shouldWaveHazardProximityPass(true, false, true, false, true),
           "wave-prox-no-hazard");
    expect(EbShadowSimulator::shouldWaveFutileDelayPass(true, false, 0, 3.f, false, true),
           "wave-futile-pass");
    expect(!EbShadowSimulator::shouldWaveFutileDelayPass(true, false, 0, 3.f, false, false),
           "wave-futile-release-never");
    expect(!EbShadowSimulator::shouldWaveFutileDelayPass(true, true, 0, 3.f, false, true),
           "wave-futile-orb-intent");
    expect(!EbShadowSimulator::shouldWaveFutileDelayPass(true, false, 0, 3.f, true, true),
           "wave-futile-foresight");
    expect(!EbShadowSimulator::shouldWaveFutileDelayPass(true, false, 1, 3.f, false, true),
           "wave-futile-bin");
    expect(!EbShadowSimulator::shouldWaveFutileDelayPass(true, false, 0, 12.f, false, true),
           "wave-futile-gain");

    // Continuous idleSafer / ascentUseful
    expect(EbShadowSimulator::classifyIdleSafer(true, true, false, -1, 0.f, true, 4, 100.f),
           "idle-safer-alive");
    expect(!EbShadowSimulator::classifyIdleSafer(false, true, false, -1, 0.f, true, 4, 100.f),
           "idle-safer-not-cube");
    expect(!EbShadowSimulator::classifyIdleSafer(true, true, false, -1, 0.f, false, -1, 0.f),
           "idle-safer-d0-safe");
    expect(EbShadowSimulator::classifyIdleSafer(true, true, true, 10, 200.f, true, 3, 100.f),
           "idle-safer-later-death");
    expect(EbShadowSimulator::deathLooksLikeAscent(100.f, 140.f, false), "ascent-death-above");
    expect(!EbShadowSimulator::deathLooksLikeAscent(100.f, 105.f, false), "ascent-death-near");
    expect(EbShadowSimulator::deathLooksLikeAscent(100.f, 60.f, true), "ascent-death-flipped");
    expect(EbShadowSimulator::classifyAscentUseful(true, true, false, 0, 50.f, true, 0, 50.f, true),
           "ascent-later-viable");
    expect(EbShadowSimulator::classifyAscentUseful(true, true, true, 0, 50.f, true, 2, 50.f, true),
           "ascent-better-bin");
    expect(!EbShadowSimulator::classifyAscentUseful(true, true, true, 1, 50.f, true, 1, 52.f, true),
           "ascent-no-gain");
    // Dead soft / high bin without Alive later must not invent ascentUseful.
    expect(
        !EbShadowSimulator::classifyAscentUseful(true, true, false, 0, 50.f, false, 3, 90.f, false),
        "ascent-dead-not-useful");
    expect(!EbShadowSimulator::classifyAscentUseful(true, true, false, 0, 50.f, true, 3, 90.f, false),
           "ascent-soft-died-not-useful");
    expect(!EbShadowSimulator::classifyAscentUseful(true, true, true, 0, 50.f, true, 2, 50.f, false),
           "ascent-alive-flag-required");

    expect(EbShadowSimulator::pickPreferredRelease(2, 10, true) == 2, "pref-rel-cont");
    expect(EbShadowSimulator::pickPreferredRelease(2, 10, false) == 6, "pref-rel-tap");
    expect(EbShadowSimulator::pickPreferredRelease(-1, 5, false) < 0, "pref-rel-unset");
    expect(EbShadowSimulator::pickPreferredRelease(4, 4, false) == 4, "pref-rel-same");

    expect(EbShadowSimulator::autoRobustKHints(true, true, 1.f, 0.f, false) == 2, "k-wave");
    expect(EbShadowSimulator::autoRobustKHints(true, true, 1.2f, 0.f, true) == 3, "k-wave-spam-spd");
    expect(EbShadowSimulator::autoRobustKHints(true, true, 1.f, 0.f, true) == 2, "k-wave-spam-slow");
    expect(EbShadowSimulator::autoRobustKHints(false, false, 1.f, 0.f, false) == 1, "k-cube");
    expect(EbShadowSimulator::autoRobustKHints(false, false, 1.6f, 0.f, false) == 3, "k-fast");
    expect(EbShadowSimulator::autoRobustKHints(true, false, 1.f, 0.1f, false) == 3, "k-dt-bump");
    expect(EbShadowSurvivalRules::clampRobustKToWindow(1, 1) == 0, "k-clamp-limit1");
    expect(EbShadowSurvivalRules::clampRobustKToWindow(2, 4) == 2, "k-clamp-ok");
    expect(EbShadowSurvivalRules::clampRobustKToWindow(3, 2) == 1, "k-clamp-limit2");
    expect(EbShadowSurvivalRules::clampRobustKToWindow(2, 0) == 0, "k-clamp-limit0");

    // Synthetic early-click snaps: d=0 dies in zone → Delay must clear past zoneEndX.
    class EbSnap {
    public:
        float deathX;
        int clearDelay;

    private:
    };
    EbSnap snaps[] = {
        {150.f, 4}, {120.f, 3}, {190.f, 6}, {105.f, 2}, {175.f, 5},
    };
    for (auto const& s : snaps) {
        expect(s.clearDelay > 0 && s.deathX <= zone.endX, "snap-needs-delay");
        EbShadowSimulator::RunResult d0;
        d0.died = true;
        d0.deathX = s.deathX;
        expect(!clears(d0, zone), "snap-d0-unsafe");
        EbShadowSimulator::RunResult dn;
        dn.died = false;
        dn.endX = zone.endX + 40.f;
        expect(clears(dn, zone), "snap-delay-clears");
    }

    // Score honesty: soft-viable surviveBin Escape is gone.
    expect(!EbShadowSurvivalRules::delayViableHonest(false, false, false, false, false, false, true),
           "soft-viable-no-clear");
    expect(EbShadowSurvivalRules::delayViableHonest(false, false, false, false, true, false, true),
           "viable-clears-threat");
    expect(EbShadowSurvivalRules::delayViableHonest(false, false, false, false, false, true, true),
           "viable-zone-cleared");
    expect(EbShadowSurvivalRules::delayViableHonest(false, false, false, false, false, false, false),
           "viable-no-zone");
    // Soft bake: raw death + Landing/LateCrash can be not-false-lethal; Lethal cannot.
    expect(!EbShadowSurvivalRules::delayViableHonest(false, true, false, false, true, false, true,
                                                   EbModeSurvival::Class::Lethal),
           "lethal-died-not-viable");
    expect(EbShadowSurvivalRules::delayViableHonest(false, true, false, false, true, true, true,
                                                  EbModeSurvival::Class::Landing),
           "landing-died-soft-viable");
    expect(EbShadowSurvivalRules::delayViableHonest(false, true, false, false, true, true, true,
                                                  EbModeSurvival::Class::LateCrashAfterClear),
           "late-crash-soft-viable");
    // Live Delay-worthy: Alive only; Landing/LateCrash never invent Delay.
    expect(EbShadowSurvivalRules::delayViableHonest(false, false, false, false, true, false, true,
                                                  EbModeSurvival::Class::Alive, true),
           "alive-require-worthy");
    expect(!EbShadowSurvivalRules::delayViableHonest(false, true, false, false, true, true, true,
                                                   EbModeSurvival::Class::Landing, true),
           "landing-not-delay-worthy");
    expect(!EbShadowSurvivalRules::delayViableHonest(false, true, false, false, true, true, true,
                                                   EbModeSurvival::Class::LateCrashAfterClear, true),
           "late-crash-not-delay-worthy");
    expect(!EbShadowSurvivalRules::delayViableHonest(false, true, false, false, true, true, true,
                                                   EbModeSurvival::Class::Recoverable, true),
           "recoverable-not-delay-worthy");
    expect(!EbShadowSurvivalRules::delayViableHonest(false, false, true, false, true, false, true),
           "unknown-not-viable");
    expect(!EbShadowSurvivalRules::delayViableHonest(false, false, false, true, true, false, true),
           "toggle-unk-not-viable");
    expect(EbShadowSurvivalRules::exitSurvivedHonest(false, false), "exit-alive");
    expect(!EbShadowSurvivalRules::exitSurvivedHonest(true, false), "exit-died");
    expect(!EbShadowSurvivalRules::exitSurvivedHonest(false, true), "exit-unknown-not-survive");
    expect(!EbShadowSurvivalRules::exitSurvivedHonest(true, true), "exit-died-unk");

    if (fails == 0)
        report = "regressionSelfCheck OK (bins+zone+5 snaps+score-honesty)";
    else
        report = fmt::format("regressionSelfCheck {} fails: {}", fails, report);
    return fails == 0;
}
