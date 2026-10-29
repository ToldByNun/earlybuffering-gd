#include "DevRejectLedger.hpp"

#include <algorithm>
#include <cstring>
#include <fmt/format.h>

char const* DevRejectLedger::statusId(Status s) {
    switch (s) {
        case Status::Chosen: return "chosen";
        case Status::Evaluated: return "evaluated";
        case Status::Rejected: return "rejected";
        case Status::Skipped: return "skipped";
    }
    return "skipped";
}

bool DevRejectLedger::parseStatus(std::string const& s, Status& out) {
    if (s == "chosen") { out = Status::Chosen; return true; }
    if (s == "evaluated") { out = Status::Evaluated; return true; }
    if (s == "rejected") { out = Status::Rejected; return true; }
    if (s == "skipped") { out = Status::Skipped; return true; }
    return false;
}

char const* DevRejectLedger::rejectCodeId(RejectCode c) {
    switch (c) {
        case RejectCode::Chosen: return "chosen";
        case RejectCode::NotViable: return "not-viable";
        case RejectCode::DiedBeforeAction: return "died-before-action";
        case RejectCode::WorseSurviveBin: return "worse-survive-bin";
        case RejectCode::WorseEndX: return "worse-endx";
        case RejectCode::OutsideOrbWindow: return "outside-orb-window";
        case RejectCode::RobustKFailed: return "robust-k-failed";
        case RejectCode::BudgetNotReached: return "budget-not-reached";
        case RejectCode::PreferBandEarlyExit: return "prefer-band-early-exit";
        case RejectCode::WaveFutile: return "wave-futile";
        case RejectCode::WaveHazardProximity: return "wave-hazard-proximity";
        case RejectCode::DualFailed: return "dual-failed";
        case RejectCode::UnknownTrunc: return "unknown-trunc";
        case RejectCode::LegacyEarlierKept: return "legacy-earlier-kept";
        case RejectCode::VerifyFailed: return "verify-failed";
        case RejectCode::LearningDisplaced: return "learning-displaced";
    }
    return "not-viable";
}

bool DevRejectLedger::parseRejectCode(std::string const& s, RejectCode& out) {
    if (s == "chosen") { out = RejectCode::Chosen; return true; }
    if (s == "not-viable") { out = RejectCode::NotViable; return true; }
    if (s == "died-before-action") { out = RejectCode::DiedBeforeAction; return true; }
    if (s == "worse-survive-bin") { out = RejectCode::WorseSurviveBin; return true; }
    if (s == "worse-endx") { out = RejectCode::WorseEndX; return true; }
    if (s == "outside-orb-window") { out = RejectCode::OutsideOrbWindow; return true; }
    if (s == "robust-k-failed") { out = RejectCode::RobustKFailed; return true; }
    if (s == "budget-not-reached") { out = RejectCode::BudgetNotReached; return true; }
    if (s == "prefer-band-early-exit") { out = RejectCode::PreferBandEarlyExit; return true; }
    if (s == "wave-futile") { out = RejectCode::WaveFutile; return true; }
    if (s == "wave-hazard-proximity") { out = RejectCode::WaveHazardProximity; return true; }
    if (s == "dual-failed") { out = RejectCode::DualFailed; return true; }
    if (s == "unknown-trunc") { out = RejectCode::UnknownTrunc; return true; }
    if (s == "legacy-earlier-kept") { out = RejectCode::LegacyEarlierKept; return true; }
    if (s == "verify-failed") { out = RejectCode::VerifyFailed; return true; }
    if (s == "learning-displaced") { out = RejectCode::LearningDisplaced; return true; }
    return false;
}

char const* DevRejectLedger::attributionId(Attribution a) {
    switch (a) {
        case Attribution::ChosenMatchesBestSurviving:
            return "chosen-matches-best-surviving";
        case Attribution::ChosenDiffersBestSurviving:
            return "chosen-differs-best-surviving";
        case Attribution::BestSurvivingAbsent: return "best-surviving-absent";
        case Attribution::ApproxOnly: return "approx-only";
        case Attribution::NotReproducible: return "not-reproducible";
        case Attribution::LearningNudged: return "learning-nudged";
        case Attribution::LearningFloored: return "learning-floored";
        case Attribution::RecheckSlide: return "recheck-slide";
        case Attribution::HusDeadline: return "hus-deadline";
        case Attribution::VerifyRefused: return "verify-refused";
        case Attribution::GatePass: return "gate-pass";
        case Attribution::LimitExcluded: return "limit-excluded";
        case Attribution::DualFailed: return "dual-failed";
        case Attribution::UnknownTrunc: return "unknown-trunc";
        case Attribution::SkippedByPlanner: return "skipped-by-planner";
    }
    return "gate-pass";
}

bool DevRejectLedger::parseAttribution(std::string const& s, Attribution& out) {
    if (s == "chosen-matches-best-surviving") {
        out = Attribution::ChosenMatchesBestSurviving;
        return true;
    }
    if (s == "chosen-differs-best-surviving") {
        out = Attribution::ChosenDiffersBestSurviving;
        return true;
    }
    if (s == "best-surviving-absent") { out = Attribution::BestSurvivingAbsent; return true; }
    if (s == "approx-only") { out = Attribution::ApproxOnly; return true; }
    if (s == "not-reproducible") { out = Attribution::NotReproducible; return true; }
    if (s == "learning-nudged") { out = Attribution::LearningNudged; return true; }
    if (s == "learning-floored") { out = Attribution::LearningFloored; return true; }
    if (s == "recheck-slide") { out = Attribution::RecheckSlide; return true; }
    if (s == "hus-deadline") { out = Attribution::HusDeadline; return true; }
    if (s == "verify-refused") { out = Attribution::VerifyRefused; return true; }
    if (s == "gate-pass") { out = Attribution::GatePass; return true; }
    if (s == "limit-excluded") { out = Attribution::LimitExcluded; return true; }
    if (s == "dual-failed") { out = Attribution::DualFailed; return true; }
    if (s == "unknown-trunc") { out = Attribution::UnknownTrunc; return true; }
    if (s == "skipped-by-planner") { out = Attribution::SkippedByPlanner; return true; }
    return false;
}

std::string DevRejectLedger::clampDetail(std::string s) {
    if (static_cast<int>(s.size()) > kMaxDetailLen) {
        s.resize(static_cast<size_t>(kMaxDetailLen));
    }
    return s;
}

DevRejectLedger::RejectCode DevRejectLedger::skipCode(EarlyExit early) {
    switch (early) {
        case EarlyExit::Budget: return RejectCode::BudgetNotReached;
        case EarlyExit::PreferBand: return RejectCode::PreferBandEarlyExit;
        case EarlyExit::WaveProximity: return RejectCode::WaveHazardProximity;
        case EarlyExit::WaveFutile: return RejectCode::WaveFutile;
        case EarlyExit::NoThreat: return RejectCode::NotViable;
        case EarlyExit::None: return RejectCode::BudgetNotReached;
    }
    return RejectCode::BudgetNotReached;
}

DevRejectLedger::RejectCode DevRejectLedger::classifyRejected(
    CandSnap const& snap,
    CandSnap const* chosen,
    BuildInput const& in,
    int delay
) {
    if (snap.dualFailed) return RejectCode::DualFailed;
    if (snap.diedBeforeAction) return RejectCode::DiedBeforeAction;
    if (snap.verifyFailed) return RejectCode::VerifyFailed;
    if (snap.robustKFailed) return RejectCode::RobustKFailed;
    if (snap.unknownTrunc && !snap.viable) return RejectCode::UnknownTrunc;

    if (in.earlyExit == EarlyExit::WaveFutile && snap.evaluated) {
        return RejectCode::WaveFutile;
    }

    if (snap.viable) {
        if (in.orbIntent && in.actMin >= 0 && in.actMax >= in.actMin
            && (delay < in.actMin || delay > in.actMax)) {
            return RejectCode::OutsideOrbWindow;
        }
        if (chosen) {
            if (in.legacyPick && delay > chosen->surviveBin
                && snap.surviveBin <= chosen->surviveBin
                && delay > 0) {
                // Legacy kept an earlier/safer pick; mark later survivors.
            }
            if (in.legacyPick && delay != 0 && chosen
                && delay > (in.doPass ? 0 : in.chosenDelay)
                && in.chosenDelay >= 0 && delay > in.chosenDelay) {
                // Later viable lost to earlier legacy pick.
                if (in.doPass || in.chosenDelay < delay) {
                    return RejectCode::LegacyEarlierKept;
                }
            }
            if (snap.surviveBin < chosen->surviveBin) {
                return RejectCode::WorseSurviveBin;
            }
            if (snap.endX + 1e-3f < chosen->endX) {
                return RejectCode::WorseEndX;
            }
            if (in.legacyPick && in.chosenDelay >= 0 && delay > in.chosenDelay) {
                return RejectCode::LegacyEarlierKept;
            }
        }
        return RejectCode::WorseEndX;
    }
    return RejectCode::NotViable;
}

DevRejectLedger::Ledger DevRejectLedger::build(BuildInput const& in) {
    Ledger out;
    if (in.limit < 0) return out;

    int const n = in.limit + 1;
    out.notes.reserve(static_cast<size_t>(n));

    CandSnap const* chosenSnap = nullptr;
    if (in.chosenDelay >= 0 && in.chosenDelay <= in.limit
        && static_cast<int>(in.snaps.size()) > in.chosenDelay
        && in.snaps[static_cast<size_t>(in.chosenDelay)].evaluated) {
        chosenSnap = &in.snaps[static_cast<size_t>(in.chosenDelay)];
    }

    bool const scoresWanted = in.includeScores;
    if (n > kMaxCandNotes && scoresWanted) {
        out.truncated = true;
    }

    for (int d = 0; d < n; ++d) {
        CandNote note;
        note.delay = d;

        CandSnap snap{};
        if (d < static_cast<int>(in.snaps.size())) {
            snap = in.snaps[static_cast<size_t>(d)];
        }

        bool const isChosen = (in.chosenDelay == d)
            || (in.doPass && d == 0 && in.chosenDelay <= 0);

        if (isChosen && (snap.evaluated || d == 0 || in.doPass)) {
            note.status = Status::Chosen;
            note.rejectCode = RejectCode::Chosen;
            note.detail = clampDetail(fmt::format("d={}", d));
        }
        else if (!snap.evaluated) {
            note.status = Status::Skipped;
            note.rejectCode = skipCode(in.earlyExit);
            if (in.earlyExit == EarlyExit::NoThreat) {
                note.detail = clampDetail("no-threat");
            }
            else {
                note.detail = clampDetail(
                    fmt::format("skip={}", rejectCodeId(note.rejectCode))
                );
            }
        }
        else if (!snap.viable) {
            note.status = Status::Rejected;
            note.rejectCode = classifyRejected(snap, chosenSnap, in, d);
            note.detail = clampDetail(fmt::format(
                "{} bin={} endX={:.1f}",
                rejectCodeId(note.rejectCode),
                snap.surviveBin,
                snap.endX
            ));
        }
        else {
            // Viable but not chosen.
            note.status = Status::Evaluated;
            if (in.earlyExit == EarlyExit::WaveFutile) {
                note.rejectCode = RejectCode::WaveFutile;
            }
            else {
                note.rejectCode = classifyRejected(snap, chosenSnap, in, d);
            }
            note.detail = clampDetail(fmt::format(
                "{} bin={} endX={:.1f}",
                rejectCodeId(note.rejectCode),
                snap.surviveBin,
                snap.endX
            ));
        }

        bool const writeScores = scoresWanted
            && (!out.truncated || d < kMaxCandNotes);
        if (writeScores && snap.evaluated) {
            note.hasScores = true;
            note.surviveBin = snap.surviveBin;
            note.endX = snap.endX;
            note.latencyTerm = in.latencyWeight * static_cast<float>(d);
            note.threatCleared = snap.clearsThreat;
            note.score = snap.endX - note.latencyTerm;
        }
        else if (out.truncated && scoresWanted && d >= kMaxCandNotes) {
            // Codes remain; scores omitted.
            note.hasScores = false;
        }

        out.notes.push_back(std::move(note));
    }

    // Ensure exactly one chosen when doPass / chosenDelay set.
    if (in.chosenDelay >= 0 && in.chosenDelay <= in.limit) {
        for (auto& note : out.notes) {
            if (note.delay == in.chosenDelay) {
                note.status = Status::Chosen;
                note.rejectCode = RejectCode::Chosen;
            }
            else if (note.status == Status::Chosen) {
                note.status = Status::Evaluated;
            }
        }
    }
    else if (in.doPass) {
        for (auto& note : out.notes) {
            if (note.delay == 0) {
                note.status = Status::Chosen;
                note.rejectCode = RejectCode::Chosen;
            }
            else if (note.status == Status::Chosen) {
                note.status = Status::Evaluated;
            }
        }
    }

    return out;
}

std::vector<DevRejectLedger::AttributionNote> DevRejectLedger::buildAttribution(
    AttributionInput const& in
) {
    std::vector<AttributionNote> out;

    if (in.gatePass && !in.hasPlanFinished) {
        AttributionNote n;
        n.code = Attribution::GatePass;
        n.detail = "gate-pass";
        out.push_back(std::move(n));
        return out;
    }

    if (in.notReproducible) {
        AttributionNote n;
        n.code = Attribution::NotReproducible;
        out.push_back(std::move(n));
    }
    else if (in.approx) {
        AttributionNote n;
        n.code = Attribution::ApproxOnly;
        out.push_back(std::move(n));
    }

    if (in.claimsAllowed && in.hasBestSurviving) {
        if (in.plannedDelay == in.bestSurvivingDelay) {
            AttributionNote n;
            n.code = Attribution::ChosenMatchesBestSurviving;
            n.detail = clampDetail(fmt::format("d={}", in.plannedDelay));
            out.push_back(std::move(n));
        }
        else {
            AttributionNote n;
            n.code = Attribution::ChosenDiffersBestSurviving;
            char const* rej = "";
            if (in.ledger) {
                for (auto const& cn : in.ledger->notes) {
                    if (cn.delay == in.bestSurvivingDelay) {
                        rej = rejectCodeId(cn.rejectCode);
                        break;
                    }
                }
            }
            n.detail = clampDetail(fmt::format(
                "planned={} bestSurv={} rej={} obj={}",
                in.plannedDelay,
                in.bestSurvivingDelay,
                rej,
                in.objectiveId ? in.objectiveId : ""
            ));
            out.push_back(std::move(n));
        }
    }
    else if (in.hasPlanFinished) {
        AttributionNote n;
        n.code = Attribution::BestSurvivingAbsent;
        out.push_back(std::move(n));
    }

    if (in.learningNudged) {
        AttributionNote n;
        n.code = Attribution::LearningNudged;
        out.push_back(std::move(n));
    }
    if (in.learningFloored) {
        AttributionNote n;
        n.code = Attribution::LearningFloored;
        out.push_back(std::move(n));
    }
    if (in.recheckSlide) {
        AttributionNote n;
        n.code = Attribution::RecheckSlide;
        out.push_back(std::move(n));
    }
    if (in.husDeadline) {
        AttributionNote n;
        n.code = Attribution::HusDeadline;
        out.push_back(std::move(n));
    }
    if (in.verifyRefused) {
        AttributionNote n;
        n.code = Attribution::VerifyRefused;
        out.push_back(std::move(n));
    }
    if (in.limitExcluded) {
        AttributionNote n;
        n.code = Attribution::LimitExcluded;
        out.push_back(std::move(n));
    }

    if (in.ledger) {
        for (auto const& cn : in.ledger->notes) {
            if (cn.rejectCode == RejectCode::DualFailed) {
                AttributionNote n;
                n.code = Attribution::DualFailed;
                n.detail = clampDetail(fmt::format("d={}", cn.delay));
                out.push_back(std::move(n));
                break;
            }
        }
        for (auto const& cn : in.ledger->notes) {
            if (cn.rejectCode == RejectCode::UnknownTrunc) {
                AttributionNote n;
                n.code = Attribution::UnknownTrunc;
                n.detail = clampDetail(fmt::format("d={}", cn.delay));
                out.push_back(std::move(n));
                break;
            }
        }
        if (in.hasBestSurviving) {
            for (auto const& cn : in.ledger->notes) {
                if (cn.delay == in.bestSurvivingDelay
                    && cn.status == Status::Skipped) {
                    AttributionNote n;
                    n.code = Attribution::SkippedByPlanner;
                    n.detail = clampDetail(fmt::format(
                        "d={} {}",
                        cn.delay,
                        rejectCodeId(cn.rejectCode)
                    ));
                    out.push_back(std::move(n));
                    break;
                }
            }
        }
    }

    return out;
}

bool DevRejectLedger::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += ", ";
        report += name;
    };

    expect(
        std::strcmp(rejectCodeId(RejectCode::Chosen), "chosen") == 0,
        "code-chosen"
    );
    expect(
        std::strcmp(statusId(Status::Skipped), "skipped") == 0,
        "status-skipped"
    );
    expect(
        std::strcmp(
            attributionId(Attribution::GatePass), "gate-pass"
        ) == 0,
        "attr-gate"
    );

    RejectCode parsed = RejectCode::Chosen;
    expect(
        parseRejectCode("robust-k-failed", parsed)
            && parsed == RejectCode::RobustKFailed,
        "parse-code"
    );

    // Full ledger: every delay 0..limit once; chosen marked.
    {
        BuildInput in;
        in.limit = 3;
        in.chosenDelay = 2;
        in.latencyWeight = 0.25f;
        in.includeScores = true;
        in.snaps.resize(4);
        for (int d = 0; d <= 3; ++d) {
            in.snaps[static_cast<size_t>(d)].evaluated = true;
            in.snaps[static_cast<size_t>(d)].viable = (d != 1);
            in.snaps[static_cast<size_t>(d)].surviveBin = d;
            in.snaps[static_cast<size_t>(d)].endX = 10.f * static_cast<float>(d);
            in.snaps[static_cast<size_t>(d)].clearsThreat = true;
        }
        in.snaps[1].diedBeforeAction = true;
        in.snaps[1].viable = false;

        auto led = build(in);
        expect(static_cast<int>(led.notes.size()) == 4, "notes-n");
        expect(!led.truncated, "not-trunc");
        int chosenCount = 0;
        for (auto const& n : led.notes) {
            if (n.status == Status::Chosen) {
                ++chosenCount;
                expect(n.delay == 2, "chosen-d");
                expect(n.rejectCode == RejectCode::Chosen, "chosen-code");
            }
            if (n.delay == 1) {
                expect(n.status == Status::Rejected, "died-status");
                expect(
                    n.rejectCode == RejectCode::DiedBeforeAction, "died-code"
                );
            }
            if (n.delay == 0) {
                expect(n.hasScores && n.surviveBin == 0, "scores-0");
            }
        }
        expect(chosenCount == 1, "one-chosen");
    }

    // Skipped after wave proximity.
    {
        BuildInput in;
        in.limit = 2;
        in.doPass = true;
        in.chosenDelay = 0;
        in.earlyExit = EarlyExit::WaveProximity;
        in.snaps.resize(3);
        in.snaps[0].evaluated = true;
        in.snaps[0].viable = true;
        in.snaps[0].endX = 5.f;
        auto led = build(in);
        expect(led.notes[0].status == Status::Chosen, "prox-chosen");
        expect(
            led.notes[1].status == Status::Skipped
                && led.notes[1].rejectCode == RejectCode::WaveHazardProximity,
            "prox-skip"
        );
        expect(led.notes[2].status == Status::Skipped, "prox-skip2");
    }

    // Truncation: codes remain past kMaxCandNotes, scores drop.
    {
        BuildInput in;
        in.limit = kMaxCandNotes + 2;
        in.chosenDelay = 0;
        in.doPass = true;
        in.includeScores = true;
        in.snaps.resize(static_cast<size_t>(in.limit) + 1);
        for (int d = 0; d <= in.limit; ++d) {
            in.snaps[static_cast<size_t>(d)].evaluated = true;
            in.snaps[static_cast<size_t>(d)].viable = true;
            in.snaps[static_cast<size_t>(d)].endX = 1.f;
        }
        auto led = build(in);
        expect(led.truncated, "trunc-flag");
        expect(
            static_cast<int>(led.notes.size()) == in.limit + 1, "trunc-n"
        );
        expect(led.notes[0].hasScores, "trunc-score-low");
        expect(
            !led.notes[static_cast<size_t>(kMaxCandNotes)].hasScores,
            "trunc-score-high"
        );
        expect(
            led.notes[static_cast<size_t>(kMaxCandNotes)].rejectCode
                != RejectCode::Chosen
                || led.notes[0].status == Status::Chosen,
            "trunc-codes"
        );
    }

    // Attribution: gate-pass alone.
    {
        AttributionInput a;
        a.gatePass = true;
        a.hasPlanFinished = false;
        auto attrs = buildAttribution(a);
        expect(attrs.size() == 1, "attr-n");
        expect(attrs[0].code == Attribution::GatePass, "attr-gate-code");
    }

    // Attribution: chosen differs best surviving.
    {
        BuildInput in;
        in.limit = 2;
        in.chosenDelay = 0;
        in.doPass = true;
        in.snaps.resize(3);
        for (int d = 0; d <= 2; ++d) {
            in.snaps[static_cast<size_t>(d)].evaluated = true;
            in.snaps[static_cast<size_t>(d)].viable = true;
            in.snaps[static_cast<size_t>(d)].endX = static_cast<float>(d);
            in.snaps[static_cast<size_t>(d)].surviveBin = 1;
        }
        auto led = build(in);

        AttributionInput a;
        a.hasPlanFinished = true;
        a.claimsAllowed = true;
        a.hasBestSurviving = true;
        a.bestSurvivingDelay = 2;
        a.plannedDelay = 0;
        a.objectiveId = "survive_then_endx_v1";
        a.ledger = &led;
        auto attrs = buildAttribution(a);
        bool found = false;
        for (auto const& at : attrs) {
            if (at.code == Attribution::ChosenDiffersBestSurviving) {
                found = true;
                expect(
                    at.detail.find("bestSurv=2") != std::string::npos,
                    "attr-detail"
                );
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
