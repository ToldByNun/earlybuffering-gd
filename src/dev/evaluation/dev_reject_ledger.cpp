#include "dev_reject_ledger.hpp"
#include "attribution/dev_attribution.hpp"
#include "validation/dev_reject_checks.hpp"
#include <fmt/format.h>
#include <string>
#include <vector>

char const* DevRejectLedger::statusId(Status state) {
    switch (state) {
    case Status::Chosen:
        return "chosen";
    case Status::Evaluated:
        return "evaluated";
    case Status::Rejected:
        return "rejected";
    case Status::Skipped:
        return "skipped";
    }
    return "skipped";
}

bool DevRejectLedger::parseStatus(std::string const& s, Status& out) {
    if (s == "chosen") {
        out = Status::Chosen;
        return true;
    }
    if (s == "evaluated") {
        out = Status::Evaluated;
        return true;
    }
    if (s == "rejected") {
        out = Status::Rejected;
        return true;
    }
    if (s == "skipped") {
        out = Status::Skipped;
        return true;
    }
    return false;
}

char const* DevRejectLedger::rejectCodeId(RejectCode candidate) {
    switch (candidate) {
    case RejectCode::Chosen:
        return "chosen";
    case RejectCode::NotViable:
        return "not-viable";
    case RejectCode::DiedBeforeAction:
        return "died-before-action";
    case RejectCode::WorseSurviveBin:
        return "worse-survive-bin";
    case RejectCode::WorseEndX:
        return "worse-endx";
    case RejectCode::OutsideOrbWindow:
        return "outside-orb-window";
    case RejectCode::RobustKFailed:
        return "robust-k-failed";
    case RejectCode::BudgetNotReached:
        return "budget-not-reached";
    case RejectCode::PreferBandEarlyExit:
        return "prefer-band-early-exit";
    case RejectCode::WaveFutile:
        return "wave-futile";
    case RejectCode::WaveHazardProximity:
        return "wave-hazard-proximity";
    case RejectCode::DualFailed:
        return "dual-failed";
    case RejectCode::UnknownTrunc:
        return "unknown-trunc";
    case RejectCode::LegacyEarlierKept:
        return "legacy-earlier-kept";
    case RejectCode::VerifyFailed:
        return "verify-failed";
    case RejectCode::LearningDisplaced:
        return "learning-displaced";
    }
    return "not-viable";
}

bool DevRejectLedger::parseRejectCode(std::string const& s, RejectCode& out) {
    if (s == "chosen") {
        out = RejectCode::Chosen;
        return true;
    }
    if (s == "not-viable") {
        out = RejectCode::NotViable;
        return true;
    }
    if (s == "died-before-action") {
        out = RejectCode::DiedBeforeAction;
        return true;
    }
    if (s == "worse-survive-bin") {
        out = RejectCode::WorseSurviveBin;
        return true;
    }
    if (s == "worse-endx") {
        out = RejectCode::WorseEndX;
        return true;
    }
    if (s == "outside-orb-window") {
        out = RejectCode::OutsideOrbWindow;
        return true;
    }
    if (s == "robust-k-failed") {
        out = RejectCode::RobustKFailed;
        return true;
    }
    if (s == "budget-not-reached") {
        out = RejectCode::BudgetNotReached;
        return true;
    }
    if (s == "prefer-band-early-exit") {
        out = RejectCode::PreferBandEarlyExit;
        return true;
    }
    if (s == "wave-futile") {
        out = RejectCode::WaveFutile;
        return true;
    }
    if (s == "wave-hazard-proximity") {
        out = RejectCode::WaveHazardProximity;
        return true;
    }
    if (s == "dual-failed") {
        out = RejectCode::DualFailed;
        return true;
    }
    if (s == "unknown-trunc") {
        out = RejectCode::UnknownTrunc;
        return true;
    }
    if (s == "legacy-earlier-kept") {
        out = RejectCode::LegacyEarlierKept;
        return true;
    }
    if (s == "verify-failed") {
        out = RejectCode::VerifyFailed;
        return true;
    }
    if (s == "learning-displaced") {
        out = RejectCode::LearningDisplaced;
        return true;
    }
    return false;
}

char const* DevRejectLedger::attributionId(Attribution firstValue) {
    switch (firstValue) {
    case Attribution::ChosenMatchesBestSurviving:
        return "chosen-matches-best-surviving";
    case Attribution::ChosenDiffersBestSurviving:
        return "chosen-differs-best-surviving";
    case Attribution::BestSurvivingAbsent:
        return "best-surviving-absent";
    case Attribution::ApproxOnly:
        return "approx-only";
    case Attribution::NotReproducible:
        return "not-reproducible";
    case Attribution::LearningNudged:
        return "learning-nudged";
    case Attribution::LearningFloored:
        return "learning-floored";
    case Attribution::RecheckSlide:
        return "recheck-slide";
    case Attribution::HusDeadline:
        return "hus-deadline";
    case Attribution::VerifyRefused:
        return "verify-refused";
    case Attribution::GatePass:
        return "gate-pass";
    case Attribution::LimitExcluded:
        return "limit-excluded";
    case Attribution::DualFailed:
        return "dual-failed";
    case Attribution::UnknownTrunc:
        return "unknown-trunc";
    case Attribution::SkippedByPlanner:
        return "skipped-by-planner";
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
    if (s == "best-surviving-absent") {
        out = Attribution::BestSurvivingAbsent;
        return true;
    }
    if (s == "approx-only") {
        out = Attribution::ApproxOnly;
        return true;
    }
    if (s == "not-reproducible") {
        out = Attribution::NotReproducible;
        return true;
    }
    if (s == "learning-nudged") {
        out = Attribution::LearningNudged;
        return true;
    }
    if (s == "learning-floored") {
        out = Attribution::LearningFloored;
        return true;
    }
    if (s == "recheck-slide") {
        out = Attribution::RecheckSlide;
        return true;
    }
    if (s == "hus-deadline") {
        out = Attribution::HusDeadline;
        return true;
    }
    if (s == "verify-refused") {
        out = Attribution::VerifyRefused;
        return true;
    }
    if (s == "gate-pass") {
        out = Attribution::GatePass;
        return true;
    }
    if (s == "limit-excluded") {
        out = Attribution::LimitExcluded;
        return true;
    }
    if (s == "dual-failed") {
        out = Attribution::DualFailed;
        return true;
    }
    if (s == "unknown-trunc") {
        out = Attribution::UnknownTrunc;
        return true;
    }
    if (s == "skipped-by-planner") {
        out = Attribution::SkippedByPlanner;
        return true;
    }
    return false;
}

std::string DevRejectLedger::clampDetail(std::string text) {
    if (static_cast<int>(text.size()) > kMaxDetailLen) {
        text.resize(static_cast<size_t>(kMaxDetailLen));
    }
    return text;
}

DevRejectLedger::RejectCode DevRejectLedger::skipCode(EarlyExit early) {
    switch (early) {
    case EarlyExit::Budget:
        return RejectCode::BudgetNotReached;
    case EarlyExit::PreferBand:
        return RejectCode::PreferBandEarlyExit;
    case EarlyExit::WaveProximity:
        return RejectCode::WaveHazardProximity;
    case EarlyExit::WaveFutile:
        return RejectCode::WaveFutile;
    case EarlyExit::NoThreat:
        return RejectCode::NotViable;
    // Same-crash / prefix cut / unused order slots — not a wall-clock miss.
    case EarlyExit::None:
        return RejectCode::NotViable; // detail via skipped-by-planner attr
    }
    return RejectCode::NotViable;
}

DevRejectLedger::RejectCode DevRejectLedger::classifyRejected(CandSnap const& snap,
                                                              CandSnap const* chosen,
                                                              BuildInput const& in, int delay) {
    if (snap.dualFailed)
        return RejectCode::DualFailed;
    if (snap.diedBeforeAction)
        return RejectCode::DiedBeforeAction;
    if (snap.verifyFailed)
        return RejectCode::VerifyFailed;
    if (snap.robustKFailed)
        return RejectCode::RobustKFailed;
    if (snap.unknownTrunc && !snap.viable)
        return RejectCode::UnknownTrunc;

    if (in.earlyExit == EarlyExit::WaveFutile && snap.evaluated) {
        return RejectCode::WaveFutile;
    }

    if (snap.viable) {
        if (in.orbIntent && in.actMin >= 0 && in.actMax >= in.actMin &&
            (delay < in.actMin || delay > in.actMax)) {
            return RejectCode::OutsideOrbWindow;
        }
        if (chosen) {
            if (in.legacyPick && delay > chosen->surviveBin &&
                snap.surviveBin <= chosen->surviveBin && delay > 0) {
                // Legacy kept an earlier/safer pick; mark later survivors.
            }
            if (in.legacyPick && delay != 0 && chosen && delay > (in.doPass ? 0 : in.chosenDelay) &&
                in.chosenDelay >= 0 && delay > in.chosenDelay) {
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
    if (in.limit < 0)
        return out;

    int const count = in.limit + 1;
    out.notes.reserve(static_cast<size_t>(count));

    CandSnap const* chosenSnap = nullptr;
    if (in.chosenDelay >= 0 && in.chosenDelay <= in.limit &&
        static_cast<int>(in.snaps.size()) > in.chosenDelay &&
        in.snaps[static_cast<size_t>(in.chosenDelay)].evaluated) {
        chosenSnap = &in.snaps[static_cast<size_t>(in.chosenDelay)];
    }

    bool const scoresWanted = in.includeScores;
    if (count > kMaxCandNotes && scoresWanted) {
        out.truncated = true;
    }

    for (int delayValue = 0; delayValue < count; ++delayValue) {
        CandNote note;
        note.delay = delayValue;

        CandSnap snap{};
        if (delayValue < static_cast<int>(in.snaps.size())) {
            snap = in.snaps[static_cast<size_t>(delayValue)];
        }

        bool const isChosen =
            (in.chosenDelay == delayValue) || (in.doPass && delayValue == 0 && in.chosenDelay <= 0);

        if (isChosen && (snap.evaluated || delayValue == 0 || in.doPass)) {
            note.status = Status::Chosen;
            note.rejectCode = RejectCode::Chosen;
            note.detail = clampDetail(fmt::format("d={}", delayValue));
        } else if (!snap.evaluated) {
            note.status = Status::Skipped;
            note.rejectCode = skipCode(in.earlyExit);
            if (in.earlyExit == EarlyExit::NoThreat) {
                note.detail = clampDetail("no-threat");
            } else {
                note.detail = clampDetail(fmt::format("skip={}", rejectCodeId(note.rejectCode)));
            }
        } else if (!snap.viable) {
            note.status = Status::Rejected;
            note.rejectCode = classifyRejected(snap, chosenSnap, in, delayValue);
            note.detail =
                clampDetail(fmt::format("{} bin={} endX={:.1f}", rejectCodeId(note.rejectCode),
                                        snap.surviveBin, snap.endX));
        } else {
            // Viable but not chosen.
            note.status = Status::Evaluated;
            if (in.earlyExit == EarlyExit::WaveFutile) {
                note.rejectCode = RejectCode::WaveFutile;
            } else {
                note.rejectCode = classifyRejected(snap, chosenSnap, in, delayValue);
            }
            note.detail =
                clampDetail(fmt::format("{} bin={} endX={:.1f}", rejectCodeId(note.rejectCode),
                                        snap.surviveBin, snap.endX));
        }

        bool const writeScores = scoresWanted && (!out.truncated || delayValue < kMaxCandNotes);
        if (writeScores && snap.evaluated) {
            note.hasScores = true;
            note.surviveBin = snap.surviveBin;
            note.endX = snap.endX;
            note.latencyTerm = in.latencyWeight * static_cast<float>(delayValue);
            note.threatCleared = snap.clearsThreat;
            note.score = snap.endX - note.latencyTerm;
        } else if (out.truncated && scoresWanted && delayValue >= kMaxCandNotes) {
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
            } else if (note.status == Status::Chosen) {
                note.status = Status::Evaluated;
            }
        }
    } else if (in.doPass) {
        for (auto& note : out.notes) {
            if (note.delay == 0) {
                note.status = Status::Chosen;
                note.rejectCode = RejectCode::Chosen;
            } else if (note.status == Status::Chosen) {
                note.status = Status::Evaluated;
            }
        }
    }

    return out;
}

std::vector<DevRejectLedger::AttributionNote>
DevRejectLedger::buildAttribution(AttributionInput const& in) {
    return DevAttribution::buildAttribution(in);
}

bool DevRejectLedger::selfCheck(std::string& report) {
    return DevRejectChecks::selfCheck(report);
}
