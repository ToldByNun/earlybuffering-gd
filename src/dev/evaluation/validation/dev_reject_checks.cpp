#include "dev_reject_checks.hpp"
#include "../dev_reject_ledger.hpp"
#include <cstring>
#include <fmt/format.h>
#include <string>

bool EbDevRejectChecks::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += ", ";
        report += name;
    };

    expect(std::strcmp(EbDevRejectLedger::rejectCodeId(EbDevRejectLedger::RejectCode::Chosen),
                       "chosen") == 0,
           "code-chosen");
    expect(std::strcmp(EbDevRejectLedger::statusId(EbDevRejectLedger::Status::Skipped), "skipped") == 0,
           "status-skipped");
    expect(std::strcmp(EbDevRejectLedger::attributionId(EbDevRejectLedger::Attribution::GatePass),
                       "gate-pass") == 0,
           "attr-gate");

    EbDevRejectLedger::RejectCode parsed = EbDevRejectLedger::RejectCode::Chosen;
    expect(EbDevRejectLedger::parseRejectCode("robust-k-failed", parsed) &&
               parsed == EbDevRejectLedger::RejectCode::RobustKFailed,
           "parse-code");

    // Full ledger: every delay 0..limit once; chosen marked.
    {
        EbDevRejectLedger::BuildInput in;
        in.limit = 3;
        in.chosenDelay = 2;
        in.latencyWeight = 0.25f;
        in.includeScores = true;
        in.snaps.resize(4);
        for (int delay = 0; delay <= 3; ++delay) {
            in.snaps[static_cast<size_t>(delay)].evaluated = true;
            in.snaps[static_cast<size_t>(delay)].viable = (delay != 1);
            in.snaps[static_cast<size_t>(delay)].surviveBin = delay;
            in.snaps[static_cast<size_t>(delay)].endX = 10.f * static_cast<float>(delay);
            in.snaps[static_cast<size_t>(delay)].clearsThreat = true;
        }
        in.snaps[1].diedBeforeAction = true;
        in.snaps[1].viable = false;

        auto ledger = EbDevRejectLedger::build(in);
        expect(static_cast<int>(ledger.notes.size()) == 4, "notes-n");
        expect(!ledger.truncated, "not-trunc");
        int chosenCount = 0;
        for (auto const& n : ledger.notes) {
            if (n.status == EbDevRejectLedger::Status::Chosen) {
                ++chosenCount;
                expect(n.delay == 2, "chosen-d");
                expect(n.rejectCode == EbDevRejectLedger::RejectCode::Chosen, "chosen-code");
            }
            if (n.delay == 1) {
                expect(n.status == EbDevRejectLedger::Status::Rejected, "died-status");
                expect(n.rejectCode == EbDevRejectLedger::RejectCode::DiedBeforeAction, "died-code");
            }
            if (n.delay == 0) {
                expect(n.hasScores && n.surviveBin == 0, "scores-0");
            }
        }
        expect(chosenCount == 1, "one-chosen");
    }

    // Skipped after wave proximity.
    {
        EbDevRejectLedger::BuildInput in;
        in.limit = 2;
        in.doPass = true;
        in.chosenDelay = 0;
        in.earlyExit = EbDevRejectLedger::EarlyExit::WaveProximity;
        in.snaps.resize(3);
        in.snaps[0].evaluated = true;
        in.snaps[0].viable = true;
        in.snaps[0].endX = 5.f;
        auto ledger = EbDevRejectLedger::build(in);
        expect(ledger.notes[0].status == EbDevRejectLedger::Status::Chosen, "prox-chosen");
        expect(ledger.notes[1].status == EbDevRejectLedger::Status::Skipped &&
                   ledger.notes[1].rejectCode == EbDevRejectLedger::RejectCode::WaveHazardProximity,
               "prox-skip");
        expect(ledger.notes[2].status == EbDevRejectLedger::Status::Skipped, "prox-skip2");
    }

    // Same-crash / earliest-safe skips must not look like not-viable rejects.
    {
        expect(std::strcmp(EbDevRejectLedger::rejectCodeId(
                               EbDevRejectLedger::RejectCode::SameCrashCut),
                           "same-crash-cut") == 0,
               "code-same-crash");
        expect(std::strcmp(EbDevRejectLedger::rejectCodeId(
                               EbDevRejectLedger::RejectCode::EarliestSafeExit),
                           "earliest-safe-exit") == 0,
               "code-earliest");
        EbDevRejectLedger::BuildInput same;
        same.limit = 3;
        same.chosenDelay = 0;
        same.doPass = true;
        same.earlyExit = EbDevRejectLedger::EarlyExit::SameCrash;
        same.snaps.resize(4);
        same.snaps[0].evaluated = true;
        same.snaps[0].viable = false;
        same.snaps[0].endX = 1.f;
        same.snaps[1].evaluated = true;
        same.snaps[1].viable = false;
        auto sameLedger = EbDevRejectLedger::build(same);
        expect(sameLedger.notes[2].status == EbDevRejectLedger::Status::Skipped &&
                   sameLedger.notes[2].rejectCode == EbDevRejectLedger::RejectCode::SameCrashCut,
               "same-crash-skip");
        EbDevRejectLedger::BuildInput early;
        early.limit = 3;
        early.chosenDelay = 1;
        early.earlyExit = EbDevRejectLedger::EarlyExit::EarliestSafe;
        early.snaps.resize(4);
        early.snaps[0].evaluated = true;
        early.snaps[1].evaluated = true;
        early.snaps[1].viable = true;
        early.snaps[1].endX = 2.f;
        auto earlyLedger = EbDevRejectLedger::build(early);
        expect(earlyLedger.notes[2].rejectCode == EbDevRejectLedger::RejectCode::EarliestSafeExit,
               "earliest-skip");
        expect(std::strcmp(EbDevRejectLedger::rejectCodeId(
                               EbDevRejectLedger::RejectCode::PeakDeathCut),
                           "peak-death-cut") == 0,
               "code-peak-death");
        expect(EbDevRejectLedger::skipCode(EbDevRejectLedger::EarlyExit::PeakDeath) ==
                   EbDevRejectLedger::RejectCode::PeakDeathCut,
               "skip-peak-death");
        EbDevRejectLedger::BuildInput peak;
        peak.limit = 3;
        peak.chosenDelay = 0;
        peak.doPass = true;
        peak.earlyExit = EbDevRejectLedger::EarlyExit::PeakDeath;
        peak.snaps.resize(4);
        peak.snaps[0].evaluated = true;
        peak.snaps[0].viable = true;
        peak.snaps[0].endX = 2.f;
        peak.snaps[1].evaluated = true;
        peak.snaps[1].viable = false;
        auto peakLedger = EbDevRejectLedger::build(peak);
        expect(peakLedger.notes[2].status == EbDevRejectLedger::Status::Skipped &&
                   peakLedger.notes[2].rejectCode == EbDevRejectLedger::RejectCode::PeakDeathCut,
               "peak-death-skip");
    }

    // Assist Pack AP1: depth-truncated Alive → budget-not-reached, never not-viable.
    {
        EbDevRejectLedger::BuildInput in;
        in.limit = 2;
        in.chosenDelay = 0;
        in.doPass = true;
        in.snaps.resize(3);
        in.snaps[0].evaluated = true;
        in.snaps[0].viable = false;
        in.snaps[0].deathTick = 54;
        in.snaps[0].ticksRan = 55;
        in.snaps[0].hasRunTruth = true;
        in.snaps[1].evaluated = true;
        in.snaps[1].viable = false;
        in.snaps[1].budgetHit = true;
        in.snaps[1].ticksRan = 18;
        in.snaps[1].deathTick = -1;
        in.snaps[1].hasRunTruth = true;
        in.snaps[1].endX = 10.f;
        in.snaps[2].evaluated = true;
        in.snaps[2].viable = false;
        in.snaps[2].budgetHit = true;
        in.snaps[2].ticksRan = 18;
        in.snaps[2].hasRunTruth = true;
        auto ledger = EbDevRejectLedger::build(in);
        expect(ledger.notes[1].rejectCode == EbDevRejectLedger::RejectCode::BudgetNotReached,
               "ap1-depth-budget");
        expect(std::strcmp(EbDevRejectLedger::rejectCodeId(ledger.notes[1].rejectCode),
                           "budget-not-reached") == 0,
               "ap1-depth-id");
    }

    // Truncation: codes remain past kMaxCandNotes, scores drop.
    {
        EbDevRejectLedger::BuildInput in;
        in.limit = EbDevRejectLedger::kMaxCandNotes + 2;
        in.chosenDelay = 0;
        in.doPass = true;
        in.includeScores = true;
        in.snaps.resize(static_cast<size_t>(in.limit) + 1);
        for (int delay = 0; delay <= in.limit; ++delay) {
            in.snaps[static_cast<size_t>(delay)].evaluated = true;
            in.snaps[static_cast<size_t>(delay)].viable = true;
            in.snaps[static_cast<size_t>(delay)].endX = 1.f;
        }
        auto ledger = EbDevRejectLedger::build(in);
        expect(ledger.truncated, "trunc-flag");
        expect(static_cast<int>(ledger.notes.size()) == in.limit + 1, "trunc-n");
        expect(ledger.notes[0].hasScores, "trunc-score-low");
        expect(!ledger.notes[static_cast<size_t>(EbDevRejectLedger::kMaxCandNotes)].hasScores,
               "trunc-score-high");
        expect(ledger.notes[static_cast<size_t>(EbDevRejectLedger::kMaxCandNotes)].rejectCode !=
                       EbDevRejectLedger::RejectCode::Chosen ||
                   ledger.notes[0].status == EbDevRejectLedger::Status::Chosen,
               "trunc-codes");
    }

    // Attribution: gate-pass alone.
    {
        EbDevRejectLedger::AttributionInput a;
        a.gatePass = true;
        a.hasPlanFinished = false;
        auto attrs = EbDevRejectLedger::buildAttribution(a);
        expect(attrs.size() == 1, "attr-n");
        expect(attrs[0].code == EbDevRejectLedger::Attribution::GatePass, "attr-gate-code");
    }

    // Attribution: chosen differs best surviving.
    {
        EbDevRejectLedger::BuildInput in;
        in.limit = 2;
        in.chosenDelay = 0;
        in.doPass = true;
        in.snaps.resize(3);
        for (int delay = 0; delay <= 2; ++delay) {
            in.snaps[static_cast<size_t>(delay)].evaluated = true;
            in.snaps[static_cast<size_t>(delay)].viable = true;
            in.snaps[static_cast<size_t>(delay)].endX = static_cast<float>(delay);
            in.snaps[static_cast<size_t>(delay)].surviveBin = 1;
        }
        auto ledgerValue = EbDevRejectLedger::build(in);

        EbDevRejectLedger::AttributionInput a;
        a.hasPlanFinished = true;
        a.claimsAllowed = true;
        a.hasBestSurviving = true;
        a.bestSurvivingDelay = 2;
        a.plannedDelay = 0;
        a.objectiveId = "survive_then_endx_v1";
        a.ledger = &ledgerValue;
        auto attrs = EbDevRejectLedger::buildAttribution(a);
        bool found = false;
        for (auto const& at : attrs) {
            if (at.code == EbDevRejectLedger::Attribution::ChosenDiffersBestSurviving) {
                found = true;
                expect(at.detail.find("bestSurv=2") != std::string::npos, "attr-detail");
            }
        }
        expect(found, "attr-differs");
    }

    if (fails == 0) {
        report = "DevRejectLedger selfCheck OK";
        return true;
    }
    report = std::string("DevRejectLedger selfCheck fails: ") + report;
    return false;
}
