#include "dev_attribution.hpp"
#include "../dev_reject_ledger.hpp"
#include <fmt/format.h>
#include <vector>

std::vector<DevRejectLedger::AttributionNote>
DevAttribution::buildAttribution(DevRejectLedger::AttributionInput const& in) {
    std::vector<DevRejectLedger::AttributionNote> out;

    if (in.gatePass && !in.hasPlanFinished) {
        DevRejectLedger::AttributionNote n;
        n.code = DevRejectLedger::Attribution::GatePass;
        n.detail = "gate-pass";
        out.push_back(std::move(n));
        return out;
    }

    if (in.notReproducible) {
        DevRejectLedger::AttributionNote n;
        n.code = DevRejectLedger::Attribution::NotReproducible;
        out.push_back(std::move(n));
    } else if (in.approx) {
        DevRejectLedger::AttributionNote n;
        n.code = DevRejectLedger::Attribution::ApproxOnly;
        out.push_back(std::move(n));
    }

    if (in.claimsAllowed && in.hasBestSurviving) {
        if (in.plannedDelay == in.bestSurvivingDelay) {
            DevRejectLedger::AttributionNote n;
            n.code = DevRejectLedger::Attribution::ChosenMatchesBestSurviving;
            n.detail = DevRejectLedger::clampDetail(fmt::format("d={}", in.plannedDelay));
            out.push_back(std::move(n));
        } else {
            DevRejectLedger::AttributionNote n;
            n.code = DevRejectLedger::Attribution::ChosenDiffersBestSurviving;
            char const* rej = "";
            if (in.ledger) {
                for (auto const& cn : in.ledger->notes) {
                    if (cn.delay == in.bestSurvivingDelay) {
                        rej = DevRejectLedger::rejectCodeId(cn.rejectCode);
                        break;
                    }
                }
            }
            n.detail = DevRejectLedger::clampDetail(
                fmt::format("planned={} bestSurv={} rej={} obj={}", in.plannedDelay,
                            in.bestSurvivingDelay, rej, in.objectiveId ? in.objectiveId : ""));
            out.push_back(std::move(n));
        }
    } else if (in.hasPlanFinished) {
        DevRejectLedger::AttributionNote n;
        n.code = DevRejectLedger::Attribution::BestSurvivingAbsent;
        out.push_back(std::move(n));
    }

    if (in.learningNudged) {
        DevRejectLedger::AttributionNote n;
        n.code = DevRejectLedger::Attribution::LearningNudged;
        out.push_back(std::move(n));
    }
    if (in.learningFloored) {
        DevRejectLedger::AttributionNote n;
        n.code = DevRejectLedger::Attribution::LearningFloored;
        out.push_back(std::move(n));
    }
    if (in.recheckSlide) {
        DevRejectLedger::AttributionNote n;
        n.code = DevRejectLedger::Attribution::RecheckSlide;
        out.push_back(std::move(n));
    }
    if (in.husDeadline) {
        DevRejectLedger::AttributionNote n;
        n.code = DevRejectLedger::Attribution::HusDeadline;
        out.push_back(std::move(n));
    }
    if (in.verifyRefused) {
        DevRejectLedger::AttributionNote n;
        n.code = DevRejectLedger::Attribution::VerifyRefused;
        out.push_back(std::move(n));
    }
    if (in.limitExcluded) {
        DevRejectLedger::AttributionNote n;
        n.code = DevRejectLedger::Attribution::LimitExcluded;
        out.push_back(std::move(n));
    }

    if (in.ledger) {
        for (auto const& cn : in.ledger->notes) {
            if (cn.rejectCode == DevRejectLedger::RejectCode::DualFailed) {
                DevRejectLedger::AttributionNote n;
                n.code = DevRejectLedger::Attribution::DualFailed;
                n.detail = DevRejectLedger::clampDetail(fmt::format("d={}", cn.delay));
                out.push_back(std::move(n));
                break;
            }
        }
        for (auto const& cn : in.ledger->notes) {
            if (cn.rejectCode == DevRejectLedger::RejectCode::UnknownTrunc) {
                DevRejectLedger::AttributionNote n;
                n.code = DevRejectLedger::Attribution::UnknownTrunc;
                n.detail = DevRejectLedger::clampDetail(fmt::format("d={}", cn.delay));
                out.push_back(std::move(n));
                break;
            }
        }
        if (in.hasBestSurviving) {
            for (auto const& cn : in.ledger->notes) {
                if (cn.delay == in.bestSurvivingDelay &&
                    cn.status == DevRejectLedger::Status::Skipped) {
                    DevRejectLedger::AttributionNote n;
                    n.code = DevRejectLedger::Attribution::SkippedByPlanner;
                    n.detail = DevRejectLedger::clampDetail(fmt::format(
                        "d={} {}", cn.delay, DevRejectLedger::rejectCodeId(cn.rejectCode)));
                    out.push_back(std::move(n));
                    break;
                }
            }
        }
    }
    return out;
}
