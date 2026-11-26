#include "shadow_verified_contract.hpp"
#include <fmt/format.h>

bool EbShadowVerifiedContract::allEventsSimulated(EbRouteCandSemantics::ExpandJob const& job) {
    if (job.events.empty())
        return false;
    for (auto const& ev : job.events) {
        // Expanding/Proposed without simOk means the tail was never run.
        if (!ev.simOk)
            return false;
        if (ev.unknown || ev.budgetHit)
            return false;
        if (ev.state == EbRouteCandSemantics::LifeState::Rejected ||
            ev.state == EbRouteCandSemantics::LifeState::Proposed ||
            ev.state == EbRouteCandSemantics::LifeState::Expanding)
            return false;
    }
    return true;
}

bool EbShadowVerifiedContract::horizonCovers(EbRouteCandSemantics::ExpandJob const& job,
                                             int settleTicks) {
    int const settle = settleTicks < 0 ? 0 : settleTicks;
    int const need = job.lastEventDelay + settle;
    return job.horizonTicks >= need;
}

bool EbShadowVerifiedContract::rejectsUnknownOrBudget(
    EbRouteCandSemantics::ExpandJob const& job) {
    return job.budgetHit || job.unknown;
}

bool EbShadowVerifiedContract::isShadowVerified(EbRouteCandSemantics::ExpandJob const& job,
                                                int settleTicks) {
    if (rejectsUnknownOrBudget(job))
        return false;
    if (!allEventsSimulated(job))
        return false;
    if (!horizonCovers(job, settleTicks))
        return false;
    for (auto const& ev : job.events) {
        if (ev.state == EbRouteCandSemantics::LifeState::Rejected)
            return false;
    }
    return true;
}

EbRouteCandSemantics::LifeState
EbShadowVerifiedContract::classify(EbRouteCandSemantics::ExpandJob const& job, int settleTicks) {
    if (job.events.empty())
        return EbRouteCandSemantics::LifeState::Rejected;
    if (rejectsUnknownOrBudget(job))
        return EbRouteCandSemantics::LifeState::PartiallyChecked;

    bool anyHardReject = false;
    bool anyUnsimulated = false;
    for (auto const& ev : job.events) {
        if (ev.unknown || ev.budgetHit) {
            anyUnsimulated = true;
            continue;
        }
        if (ev.state == EbRouteCandSemantics::LifeState::Rejected ||
            (!ev.simOk && ev.state != EbRouteCandSemantics::LifeState::PartiallyChecked &&
             ev.state != EbRouteCandSemantics::LifeState::ShadowVerified)) {
            // Dead event that was simulated → hard reject; Proposed/Expanding → unsimulated.
            if (ev.state == EbRouteCandSemantics::LifeState::Proposed ||
                ev.state == EbRouteCandSemantics::LifeState::Expanding) {
                anyUnsimulated = true;
            } else if (!ev.simOk) {
                anyHardReject = true;
            }
            continue;
        }
        if (!ev.simOk || ev.state == EbRouteCandSemantics::LifeState::Proposed ||
            ev.state == EbRouteCandSemantics::LifeState::Expanding) {
            anyUnsimulated = true;
        }
    }
    // Old failure class: planned tail never simulated → PartiallyChecked, never SV.
    if (anyUnsimulated)
        return EbRouteCandSemantics::LifeState::PartiallyChecked;
    if (anyHardReject)
        return EbRouteCandSemantics::LifeState::Rejected;
    if (!horizonCovers(job, settleTicks))
        return EbRouteCandSemantics::LifeState::PartiallyChecked;
    return EbRouteCandSemantics::LifeState::ShadowVerified;
}

EbRouteCandSemantics::LifeState
EbShadowVerifiedContract::finalizeStrict(EbRouteCandSemantics::ExpandJob& job, int settleTicks) {
    (void)EbRouteCandSemantics::finalizeExpand(job);
    auto const strict = classify(job, settleTicks);
    job.state = strict;
    if (strict == EbRouteCandSemantics::LifeState::ShadowVerified) {
        for (auto& ev : job.events)
            ev.state = EbRouteCandSemantics::LifeState::ShadowVerified;
    } else {
        for (auto& ev : job.events) {
            if (ev.state == EbRouteCandSemantics::LifeState::ShadowVerified)
                ev.state = strict;
        }
    }
    return job.state;
}

bool EbShadowVerifiedContract::sequencesMatch(std::vector<EbAiRouteEvent> const& verified,
                                              std::vector<EbAiRouteEvent> const& scheduled) {
    if (verified.size() != scheduled.size())
        return false;
    for (size_t i = 0; i < verified.size(); ++i) {
        if (verified[i].kind != scheduled[i].kind)
            return false;
        if (verified[i].delay != scheduled[i].delay)
            return false;
    }
    return true;
}

bool EbShadowVerifiedContract::mayArmAutoplay(EbRouteCandSemantics::ExpandJob const& job,
                                              bool exploreLabeledUnverified) {
    if (exploreLabeledUnverified)
        return true; // Explore is allowed but must stay labeled unverified by caller.
    return isShadowVerified(job) && EbRouteCandSemantics::canArmJob(job);
}

bool EbShadowVerifiedContract::routeFullyVerified(std::vector<EbAiRouteEvent> const& events,
                                                  bool budgetHit, bool unknown) {
    if (events.empty() || budgetHit || unknown)
        return false;
    for (auto const& ev : events) {
        if (!ev.verified)
            return false;
    }
    return true;
}

bool EbShadowVerifiedContract::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += "; ";
        report += name;
    };

    expect(kDefaultSettleTicks >= 1, "settle-default");

    auto seed = EbRouteCandSemantics::makeSeed(1, 5, 0.8f, 4);
    auto full = EbRouteCandSemantics::beginExpand(100, seed, 0, 32);
    full.events.front().simOk = true;
    full.events.front().state = EbRouteCandSemantics::LifeState::PartiallyChecked;
    EbRouteCandSemantics::Cand release;
    release.id = 2;
    release.kind = EbAiRouteEvent::Kind::Release;
    release.delayTicks = 9;
    release.simOk = true;
    release.state = EbRouteCandSemantics::LifeState::PartiallyChecked;
    full.events.push_back(release);
    full.lastEventDelay = 9;
    expect(allEventsSimulated(full), "all-sim");
    expect(horizonCovers(full, 1), "horizon-ok");
    expect(isShadowVerified(full, 1), "full-sv");
    expect(classify(full, 1) == EbRouteCandSemantics::LifeState::ShadowVerified, "class-sv");
    expect(mayArmAutoplay(full, false), "arm-sv");

    // Horizon short for settle → not SV.
    auto shortH = full;
    shortH.horizonTicks = 9; // last=9, settle=1 → need 10
    expect(!horizonCovers(shortH, 1), "settle-block");
    expect(!isShadowVerified(shortH, 1), "settle-not-sv");
    expect(classify(shortH, 1) == EbRouteCandSemantics::LifeState::PartiallyChecked,
           "settle-partial");

    // Old failure class: tail never simulated → not ShadowVerified.
    auto tail = EbRouteCandSemantics::beginExpand(101, seed, 0, 64);
    tail.events.front().simOk = true;
    tail.events.front().state = EbRouteCandSemantics::LifeState::PartiallyChecked;
    EbRouteCandSemantics::Cand unsim;
    unsim.id = 3;
    unsim.kind = EbAiRouteEvent::Kind::Release;
    unsim.delayTicks = 12;
    unsim.simOk = false; // never run
    unsim.state = EbRouteCandSemantics::LifeState::Proposed;
    tail.events.push_back(unsim);
    tail.lastEventDelay = 12;
    expect(!allEventsSimulated(tail), "tail-unsim");
    expect(classify(tail, 1) == EbRouteCandSemantics::LifeState::PartiallyChecked,
           "tail-partial");
    expect(!isShadowVerified(tail, 1), "tail-not-sv");
    expect(!mayArmAutoplay(tail, false), "tail-no-arm");
    expect(mayArmAutoplay(tail, true), "explore-arm"); // labeled explore OK

    // Budget / unknown.
    auto bud = full;
    bud.budgetHit = true;
    expect(classify(bud, 1) == EbRouteCandSemantics::LifeState::PartiallyChecked, "budget");
    expect(!isShadowVerified(bud, 1), "budget-not-sv");
    auto unk = full;
    unk.unknown = true;
    expect(classify(unk, 1) == EbRouteCandSemantics::LifeState::PartiallyChecked, "unknown");
    expect(!isShadowVerified(unk, 1), "unknown-not-sv");

    // Rejected event.
    auto rej = EbRouteCandSemantics::beginExpand(102, seed, 0, 32);
    rej.events.front().simOk = false;
    rej.events.front().state = EbRouteCandSemantics::LifeState::Rejected;
    expect(classify(rej, 1) == EbRouteCandSemantics::LifeState::Rejected, "rejected");

    // finalizeStrict settle.
    auto strictJob = full;
    strictJob.horizonTicks = 9;
    expect(finalizeStrict(strictJob, 1) == EbRouteCandSemantics::LifeState::PartiallyChecked,
           "finalize-settle");
    auto strictOk = full;
    strictOk.horizonTicks = 16;
    expect(finalizeStrict(strictOk, 1) == EbRouteCandSemantics::LifeState::ShadowVerified,
           "finalize-ok");

    // Binder sequence match.
    auto evs = EbRouteCandSemantics::toRouteEvents(full);
    expect(sequencesMatch(evs, evs), "binder-eq");
    auto mutated = evs;
    mutated.back().delay += 1;
    expect(!sequencesMatch(evs, mutated), "binder-delay");
    mutated = evs;
    mutated.pop_back();
    expect(!sequencesMatch(evs, mutated), "binder-len");

    // Route-level full verify.
    std::vector<EbAiRouteEvent> routeEvs = evs;
    for (auto& e : routeEvs)
        e.verified = true;
    expect(routeFullyVerified(routeEvs, false, false), "route-full");
    routeEvs.back().verified = false;
    expect(!routeFullyVerified(routeEvs, false, false), "route-tail");
    expect(!routeFullyVerified(routeEvs, true, false), "route-budget");

    if (fails == 0) {
        report = "ShadowVerifiedContract selfCheck OK";
        return true;
    }
    report = fmt::format("ShadowVerifiedContract selfCheck {} fails: {}", fails, report);
    return false;
}
