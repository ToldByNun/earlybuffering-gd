#include "dev_reject_checks.hpp"
#include "../dev_reject_ledger.hpp"
#include <cstring>
#include <fmt/format.h>
#include <string>

bool DevRejectChecks::selfCheck(std::string& report) {
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

    expect(std::strcmp(DevRejectLedger::rejectCodeId(DevRejectLedger::RejectCode::Chosen),
                       "chosen") == 0,
           "code-chosen");
    expect(std::strcmp(DevRejectLedger::statusId(DevRejectLedger::Status::Skipped), "skipped") == 0,
           "status-skipped");
    expect(std::strcmp(DevRejectLedger::attributionId(DevRejectLedger::Attribution::GatePass),
                       "gate-pass") == 0,
           "attr-gate");

    DevRejectLedger::RejectCode parsed = DevRejectLedger::RejectCode::Chosen;
    expect(DevRejectLedger::parseRejectCode("robust-k-failed", parsed) &&
               parsed == DevRejectLedger::RejectCode::RobustKFailed,
           "parse-code");

    // Full ledger: every delay 0..limit once; chosen marked.
    {
        DevRejectLedger::BuildInput in;
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

        auto ledger = DevRejectLedger::build(in);
        expect(static_cast<int>(ledger.notes.size()) == 4, "notes-n");
        expect(!ledger.truncated, "not-trunc");
        int chosenCount = 0;
        for (auto const& n : ledger.notes) {
            if (n.status == DevRejectLedger::Status::Chosen) {
                ++chosenCount;
                expect(n.delay == 2, "chosen-d");
                expect(n.rejectCode == DevRejectLedger::RejectCode::Chosen, "chosen-code");
            }
            if (n.delay == 1) {
                expect(n.status == DevRejectLedger::Status::Rejected, "died-status");
                expect(n.rejectCode == DevRejectLedger::RejectCode::DiedBeforeAction, "died-code");
            }
            if (n.delay == 0) {
                expect(n.hasScores && n.surviveBin == 0, "scores-0");
            }
        }
        expect(chosenCount == 1, "one-chosen");
    }

    // Skipped after wave proximity.
    {
        DevRejectLedger::BuildInput in;
        in.limit = 2;
        in.doPass = true;
        in.chosenDelay = 0;
        in.earlyExit = DevRejectLedger::EarlyExit::WaveProximity;
        in.snaps.resize(3);
        in.snaps[0].evaluated = true;
        in.snaps[0].viable = true;
        in.snaps[0].endX = 5.f;
        auto ledger = DevRejectLedger::build(in);
        expect(ledger.notes[0].status == DevRejectLedger::Status::Chosen, "prox-chosen");
        expect(ledger.notes[1].status == DevRejectLedger::Status::Skipped &&
                   ledger.notes[1].rejectCode == DevRejectLedger::RejectCode::WaveHazardProximity,
               "prox-skip");
        expect(ledger.notes[2].status == DevRejectLedger::Status::Skipped, "prox-skip2");
    }

    // Truncation: codes remain past kMaxCandNotes, scores drop.
    {
        DevRejectLedger::BuildInput in;
        in.limit = DevRejectLedger::kMaxCandNotes + 2;
        in.chosenDelay = 0;
        in.doPass = true;
        in.includeScores = true;
        in.snaps.resize(static_cast<size_t>(in.limit) + 1);
        for (int delay = 0; delay <= in.limit; ++delay) {
            in.snaps[static_cast<size_t>(delay)].evaluated = true;
            in.snaps[static_cast<size_t>(delay)].viable = true;
            in.snaps[static_cast<size_t>(delay)].endX = 1.f;
        }
        auto ledger = DevRejectLedger::build(in);
        expect(ledger.truncated, "trunc-flag");
        expect(static_cast<int>(ledger.notes.size()) == in.limit + 1, "trunc-n");
        expect(ledger.notes[0].hasScores, "trunc-score-low");
        expect(!ledger.notes[static_cast<size_t>(DevRejectLedger::kMaxCandNotes)].hasScores,
               "trunc-score-high");
        expect(ledger.notes[static_cast<size_t>(DevRejectLedger::kMaxCandNotes)].rejectCode !=
                       DevRejectLedger::RejectCode::Chosen ||
                   ledger.notes[0].status == DevRejectLedger::Status::Chosen,
               "trunc-codes");
    }

    // Attribution: gate-pass alone.
    {
        DevRejectLedger::AttributionInput a;
        a.gatePass = true;
        a.hasPlanFinished = false;
        auto attrs = DevRejectLedger::buildAttribution(a);
        expect(attrs.size() == 1, "attr-n");
        expect(attrs[0].code == DevRejectLedger::Attribution::GatePass, "attr-gate-code");
    }

    // Attribution: chosen differs best surviving.
    {
        DevRejectLedger::BuildInput in;
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
        auto ledgerValue = DevRejectLedger::build(in);

        DevRejectLedger::AttributionInput a;
        a.hasPlanFinished = true;
        a.claimsAllowed = true;
        a.hasBestSurviving = true;
        a.bestSurvivingDelay = 2;
        a.plannedDelay = 0;
        a.objectiveId = "survive_then_endx_v1";
        a.ledger = &ledgerValue;
        auto attrs = DevRejectLedger::buildAttribution(a);
        bool found = false;
        for (auto const& at : attrs) {
            if (at.code == DevRejectLedger::Attribution::ChosenDiffersBestSurviving) {
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
