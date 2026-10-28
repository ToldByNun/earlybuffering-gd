#include "AssistLearning.hpp"
#include "LevelProfile.hpp"

#include <cstdlib>
#include <fmt/format.h>

void AssistLearning::decaySpot(GameModeKind kind, float x) {
    if (!StageConfig::autoTune()) return;
    auto& s = LevelProfile::get().spot(kind, x);
    // Pull trust gently toward 0.5 so low-trust spots can recover via explore.
    s.trust = s.trust * 0.98f + 0.5f * 0.02f;
    // Light hist decay so abandoned spots forget stale click timing.
    for (float& w : s.earlyHist) w *= 0.99f;
}

bool AssistLearning::shouldExploreDespiteLowTrust(float trust) {
    if (trust >= 0.35f) return false;
    // ~10% explore
    return (std::rand() % 10) == 0;
}

bool AssistLearning::shouldExploreHistogram(int histN) {
    if (histN >= 8) return false;
    // ~20% explore while the earliness prior is still thin.
    return (std::rand() % 5) == 0;
}

bool AssistLearning::histogramExploreEligible(
    GameModeKind kind,
    int histN,
    bool spamWouldPass,
    bool bakePreferActive,
    int diedAfterAssist
) {
    if (histN >= 8) return false;
    // Bake-prefer / Hybrid prefer-band: keep the mapped delay intact.
    if (bakePreferActive) return false;
    // Spam density: random ±1 fights ClickIntentGuard Pass / wave corridors.
    if (spamWouldPass) return false;

    switch (kind) {
        case GameModeKind::Wave:
        case GameModeKind::Ship:
        case GameModeKind::Swing:
            // Continuous spam modes — ±1 explore is a common death source.
            return false;
        default:
            break;
    }

    // Thin hist after dying on an assisted click at this spot — do not jitter.
    if (histN < 8 && diedAfterAssist > 0) return false;

    return true;
}

bool AssistLearning::allowHistogramExplore(
    GameModeKind kind,
    int histN,
    bool spamWouldPass,
    bool bakePreferActive,
    int diedAfterAssist
) {
    if (!histogramExploreEligible(
            kind, histN, spamWouldPass, bakePreferActive, diedAfterAssist
        )) {
        return false;
    }
    return shouldExploreHistogram(histN);
}

bool AssistLearning::allowTrustUpFromAssistInject() {
    // Never reinforce from own inject "saves".
    return false;
}

bool AssistLearning::exploreGateSelfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    expect(
        !histogramExploreEligible(GameModeKind::Wave, 3, false, false, 0),
        "block-wave"
    );
    expect(
        !histogramExploreEligible(GameModeKind::Ship, 3, false, false, 0),
        "block-ship"
    );
    expect(
        !histogramExploreEligible(GameModeKind::Swing, 3, false, false, 0),
        "block-swing"
    );
    expect(
        !histogramExploreEligible(GameModeKind::Cube, 3, true, false, 0),
        "block-spam"
    );
    expect(
        !histogramExploreEligible(GameModeKind::Cube, 3, false, true, 0),
        "block-bake-prefer"
    );
    expect(
        !histogramExploreEligible(GameModeKind::Cube, 4, false, false, 2),
        "block-died-assist"
    );
    expect(
        !histogramExploreEligible(GameModeKind::Cube, 8, false, false, 0),
        "block-hist-full"
    );
    expect(
        histogramExploreEligible(GameModeKind::Cube, 3, false, false, 0),
        "allow-cube-thin"
    );
    expect(
        histogramExploreEligible(GameModeKind::Ball, 5, false, false, 0),
        "allow-ball"
    );
    expect(
        histogramExploreEligible(GameModeKind::Ufo, 2, false, false, 0),
        "allow-ufo"
    );

    if (fails == 0) {
        report = "exploreGateSelfCheck OK";
        return true;
    }
    report = fmt::format("exploreGateSelfCheck {} fails: {}", fails, report);
    return false;
}
