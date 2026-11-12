#include "../src/core/assist/contracts/live_plan_contracts.hpp"
#include "../src/dev/evaluation/dev_reject_ledger.hpp"
#include <cstring>
#include <iostream>
#include <string>

// Offline Assist Pack AP1 fair-depth smoke (no Geode / no GD).
// Freezes Silent Hoveropolis A4 from research_20261113_001322:
//   Live: pass-hopeless@plan, d0 deathTick=54, later Alive ticks 43/18 as not-viable
//   Sealed: planned=0, bestSurv=10
// Proves NEW policy blocks that dishonest Hopeless / not-viable classification.
//
// Build (repo root, C++23):
//   g++ -std=c++23 tools/test_fair_depth_ap1.cpp \
//     src/core/assist/contracts/live_plan_contracts.cpp \
//     src/dev/evaluation/dev_reject_ledger.cpp \
//     -I. -o build/test_fair_depth_ap1.exe
//   (link fmt if your toolchain requires it for live_plan_contracts/dev_reject_ledger)
//   ./build/test_fair_depth_ap1.exe
//
int main() {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        std::cerr << "FAIL " << name << '\n';
        ++fails;
    };

    // --- Frozen A4 numbers (session attempt 4, second down planFinished) ---
    int const d0Ticks = 55;
    int const d0Death = 54;
    int const laterTicks = 43; // also 18 on delay 12
    int const quantum = 18;
    int const modelCap = 96;

    int const deathFloor =
        EbLivePlanContracts::adaptiveHonestAliveMinTicks(quantum, d0Death, modelCap);
    expect(deathFloor == 54, "a4-death-floor");

    // BEFORE bug: short Alive looked "evaluated dead" → Hopeless/not-viable.
    // AFTER: depth truncation vs death floor is explicit.
    expect(EbLivePlanContracts::depthTruncatedAlive(false, false, laterTicks, deathFloor),
           "a4-later-43-truncated");
    expect(EbLivePlanContracts::depthTruncatedAlive(false, false, 18, deathFloor),
           "a4-later-18-truncated");
    expect(!EbLivePlanContracts::depthTruncatedAlive(true, false, d0Ticks, deathFloor),
           "a4-d0-dead-not-trunc");
    expect(EbLivePlanContracts::hopelessBlockedByDepthTruncation(true, false),
           "a4-hopeless-blocked");

    // Seed honesty still requires covering death to count as proven Alive (AP10).
    expect(!EbLivePlanContracts::adaptiveSeedCountsAlive(laterTicks, quantum, d0Death, modelCap),
           "a4-seed-still-honest");
    expect(EbLivePlanContracts::adaptiveSeedCountsAlive(54, quantum, d0Death, modelCap),
           "a4-seed-at-death");

    // Fair depth ranking: deep d0 bin must not crush quantum later on bin alone.
    expect(!EbLivePlanContracts::fairDepthPreferCand(96, 3, 100.f, 18, 0, 103.f, quantum),
           "a4-fair-no-bin-steal");
    expect(EbLivePlanContracts::fairDepthPreferCand(96, 3, 100.f, 18, 0, 109.f, quantum),
           "a4-fair-score-gain");

    // Ledger: truncated Alive with budgetHit → budget-not-reached (not not-viable).
    {
        EbDevRejectLedger::BuildInput in;
        in.limit = 2;
        in.chosenDelay = 0;
        in.doPass = true;
        in.snaps.resize(3);
        in.snaps[0].evaluated = true;
        in.snaps[0].viable = false;
        in.snaps[0].deathTick = d0Death;
        in.snaps[0].ticksRan = d0Ticks;
        in.snaps[0].hasRunTruth = true;
        in.snaps[1].evaluated = true;
        in.snaps[1].viable = false;
        in.snaps[1].budgetHit = true;
        in.snaps[1].ticksRan = laterTicks;
        in.snaps[1].deathTick = -1;
        in.snaps[1].hasRunTruth = true;
        in.snaps[1].surviveBin = 1;
        in.snaps[1].endX = 2600.f;
        in.snaps[2].evaluated = true;
        in.snaps[2].viable = false;
        in.snaps[2].budgetHit = true;
        in.snaps[2].ticksRan = 18;
        in.snaps[2].hasRunTruth = true;
        auto ledger = EbDevRejectLedger::build(in);
        expect(ledger.notes[1].rejectCode == EbDevRejectLedger::RejectCode::BudgetNotReached,
               "a4-ledger-budget");
        expect(std::strcmp(EbDevRejectLedger::rejectCodeId(ledger.notes[1].rejectCode),
                           "budget-not-reached") == 0,
               "a4-ledger-id");
        // Without budgetHit flag the OLD path would stay not-viable — prove the flag matters.
        in.snaps[1].budgetHit = false;
        auto oldStyle = EbDevRejectLedger::build(in);
        expect(oldStyle.notes[1].rejectCode == EbDevRejectLedger::RejectCode::NotViable,
               "a4-old-not-viable-without-flag");
    }

    // Contract selfCheck includes AP1 expects.
    std::string report;
    expect(EbLivePlanContracts::selfCheck(report), "live-plan-contracts-selfcheck");
    if (!report.empty())
        std::cout << report << '\n';

    std::cout << "Fair depth AP1 smoke: " << fails << " failures\n";
    return fails ? 1 : 0;
}
