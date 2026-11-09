#include "dev_attribution.hpp"
#include "../dev_reject_ledger.hpp"
#include <fmt/format.h>
#include <vector>

std::vector<EbDevRejectLedger::AttributionNote>
EbDevAttribution::buildAttribution(EbDevRejectLedger::AttributionInput const& in) {
    std::vector<EbDevRejectLedger::AttributionNote> out;

    if (in.gatePass && !in.hasPlanFinished) {
        EbDevRejectLedger::AttributionNote n;
        n.code = EbDevRejectLedger::Attribution::GatePass;
        n.detail = "gate-pass";
        out.push_back(std::move(n));
        return out;
    }

    if (in.notReproducible) {
        EbDevRejectLedger::AttributionNote n;
        n.code = EbDevRejectLedger::Attribution::NotReproducible;
        out.push_back(std::move(n));
    } else if (in.approx) {
        EbDevRejectLedger::AttributionNote n;
        n.code = EbDevRejectLedger::Attribution::ApproxOnly;
        out.push_back(std::move(n));
    }

    if (in.claimsAllowed && in.hasBestSurviving) {
        if (in.plannedDelay == in.bestSurvivingDelay) {
            EbDevRejectLedger::AttributionNote n;
            n.code = EbDevRejectLedger::Attribution::ChosenMatchesBestSurviving;
            n.detail = EbDevRejectLedger::clampDetail(fmt::format("d={}", in.plannedDelay));
            out.push_back(std::move(n));
        } else {
            EbDevRejectLedger::AttributionNote n;
            n.code = EbDevRejectLedger::Attribution::ChosenDiffersBestSurviving;
            char const* rej = "";
            if (in.ledger) {
                for (auto const& cn : in.ledger->notes) {
                    if (cn.delay == in.bestSurvivingDelay) {
                        rej = EbDevRejectLedger::rejectCodeId(cn.rejectCode);
                        break;
                    }
                }
            }
            n.detail = EbDevRejectLedger::clampDetail(
                fmt::format("planned={} bestSurv={} rej={} obj={}", in.plannedDelay,
                            in.bestSurvivingDelay, rej, in.objectiveId ? in.objectiveId : ""));
            out.push_back(std::move(n));
        }
    } else if (in.hasPlanFinished) {
        EbDevRejectLedger::AttributionNote n;
        n.code = EbDevRejectLedger::Attribution::BestSurvivingAbsent;
        out.push_back(std::move(n));
    }

    if (in.learningNudged) {
        EbDevRejectLedger::AttributionNote n;
        n.code = EbDevRejectLedger::Attribution::LearningNudged;
        out.push_back(std::move(n));
    }
    if (in.learningFloored) {
        EbDevRejectLedger::AttributionNote n;
        n.code = EbDevRejectLedger::Attribution::LearningFloored;
        out.push_back(std::move(n));
    }
    if (in.recheckSlide) {
        EbDevRejectLedger::AttributionNote n;
        n.code = EbDevRejectLedger::Attribution::RecheckSlide;
        out.push_back(std::move(n));
    }
    if (in.husDeadline) {
        EbDevRejectLedger::AttributionNote n;
        n.code = EbDevRejectLedger::Attribution::HusDeadline;
        out.push_back(std::move(n));
    }
    if (in.verifyRefused) {
        EbDevRejectLedger::AttributionNote n;
        n.code = EbDevRejectLedger::Attribution::VerifyRefused;
        out.push_back(std::move(n));
    }
    if (in.limitExcluded) {
        EbDevRejectLedger::AttributionNote n;
        n.code = EbDevRejectLedger::Attribution::LimitExcluded;
        out.push_back(std::move(n));
    }

    if (in.ledger) {
        for (auto const& cn : in.ledger->notes) {
            if (cn.rejectCode == EbDevRejectLedger::RejectCode::DualFailed) {
                EbDevRejectLedger::AttributionNote n;
                n.code = EbDevRejectLedger::Attribution::DualFailed;
                n.detail = EbDevRejectLedger::clampDetail(fmt::format("d={}", cn.delay));
                out.push_back(std::move(n));
                break;
            }
        }
        for (auto const& cn : in.ledger->notes) {
            if (cn.rejectCode == EbDevRejectLedger::RejectCode::UnknownTrunc) {
                EbDevRejectLedger::AttributionNote n;
                n.code = EbDevRejectLedger::Attribution::UnknownTrunc;
                n.detail = EbDevRejectLedger::clampDetail(fmt::format("d={}", cn.delay));
                out.push_back(std::move(n));
                break;
            }
        }
        if (in.hasBestSurviving) {
            for (auto const& cn : in.ledger->notes) {
                if (cn.delay == in.bestSurvivingDelay &&
                    cn.status == EbDevRejectLedger::Status::Skipped) {
                    EbDevRejectLedger::AttributionNote n;
                    n.code = EbDevRejectLedger::Attribution::SkippedByPlanner;
                    n.detail = EbDevRejectLedger::clampDetail(fmt::format(
                        "d={} {}", cn.delay, EbDevRejectLedger::rejectCodeId(cn.rejectCode)));
                    out.push_back(std::move(n));
                    break;
                }
            }
        }
    }
    return out;
}
