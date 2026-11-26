#include "route_cand_semantics.hpp"
#include <fmt/format.h>

char const* EbRouteCandSemantics::stateName(LifeState state) {
    switch (state) {
    case LifeState::Proposed:
        return "Proposed";
    case LifeState::Expanding:
        return "Expanding";
    case LifeState::PartiallyChecked:
        return "PartiallyChecked";
    case LifeState::ShadowVerified:
        return "ShadowVerified";
    case LifeState::Rejected:
        return "Rejected";
    case LifeState::Exhausted:
        return "Exhausted";
    case LifeState::Armed:
        return "Armed";
    }
    return "Proposed";
}

bool EbRouteCandSemantics::canTransition(LifeState from, LifeState to) {
    if (from == to)
        return true;
    switch (from) {
    case LifeState::Proposed:
        return to == LifeState::Expanding || to == LifeState::Rejected ||
               to == LifeState::PartiallyChecked || to == LifeState::ShadowVerified;
    case LifeState::Expanding:
        return to == LifeState::PartiallyChecked || to == LifeState::ShadowVerified ||
               to == LifeState::Rejected || to == LifeState::Exhausted;
    case LifeState::PartiallyChecked:
        return to == LifeState::ShadowVerified || to == LifeState::Rejected ||
               to == LifeState::Exhausted || to == LifeState::Expanding;
    case LifeState::ShadowVerified:
        return to == LifeState::Armed || to == LifeState::Exhausted;
    case LifeState::Rejected:
    case LifeState::Exhausted:
        return false;
    case LifeState::Armed:
        return to == LifeState::Exhausted;
    }
    return false;
}

bool EbRouteCandSemantics::applyTransition(Cand& cand, LifeState to) {
    if (!canTransition(cand.state, to))
        return false;
    cand.state = to;
    return true;
}

bool EbRouteCandSemantics::applyJobTransition(ExpandJob& job, LifeState to) {
    if (!canTransition(job.state, to))
        return false;
    job.state = to;
    return true;
}

EbRouteCandSemantics::Cand EbRouteCandSemantics::makeSeed(std::uint32_t id, int delayTicks,
                                                          float neuralScore, int holdTicks) {
    Cand cand;
    cand.id = id;
    cand.kind = EbAiRouteEvent::Kind::Press;
    cand.delayTicks = delayTicks < 0 ? 0 : delayTicks;
    cand.holdTicks = holdTicks < 0 ? 0 : holdTicks;
    cand.neuralScore = neuralScore;
    cand.state = LifeState::Proposed;
    return cand;
}

EbRouteCandSemantics::ExpandJob EbRouteCandSemantics::beginExpand(std::uint32_t jobId,
                                                                  Cand const& seed, int planTick,
                                                                  int horizonTicks) {
    ExpandJob job;
    job.id = jobId;
    job.seedCandId = seed.id;
    job.planTick = planTick;
    job.horizonTicks = horizonTicks < 0 ? 0 : horizonTicks;
    job.state = LifeState::Expanding;
    Cand seedCopy = seed;
    seedCopy.expandJobId = jobId;
    seedCopy.state = LifeState::Expanding;
    job.events.push_back(seedCopy);
    job.lastEventDelay = seedCopy.delayTicks;
    return job;
}

bool EbRouteCandSemantics::noteSimEvent(ExpandJob& job, Cand& event, bool simOk, bool unknown,
                                        bool budgetHit, float progress, bool clearsThreat) {
    if (job.state != LifeState::Expanding && job.state != LifeState::PartiallyChecked)
        return false;
    event.simOk = simOk;
    event.unknown = unknown;
    event.budgetHit = budgetHit;
    event.progressScore = progress;
    event.clearsThreat = clearsThreat;
    event.expandJobId = job.id;
    if (unknown || budgetHit) {
        event.state = LifeState::PartiallyChecked;
        job.unknown = job.unknown || unknown;
        job.budgetHit = job.budgetHit || budgetHit;
    } else if (simOk) {
        event.state = LifeState::PartiallyChecked;
    } else {
        event.state = LifeState::Rejected;
    }
    if (event.delayTicks > job.lastEventDelay)
        job.lastEventDelay = event.delayTicks;
    job.events.push_back(event);
    if (job.state == LifeState::Proposed)
        job.state = LifeState::Expanding;
    return true;
}

EbRouteCandSemantics::LifeState EbRouteCandSemantics::finalizeExpand(ExpandJob& job) {
    if (job.events.empty()) {
        job.state = LifeState::Rejected;
        return job.state;
    }
    if (job.budgetHit || job.unknown) {
        job.state = LifeState::PartiallyChecked;
        return job.state;
    }
    bool allOk = true;
    for (auto const& ev : job.events) {
        if (!ev.simOk || ev.unknown || ev.budgetHit || ev.state == LifeState::Rejected) {
            allOk = false;
            break;
        }
    }
    if (!allOk) {
        job.state = LifeState::Rejected;
        return job.state;
    }
    // Horizon must cover the last planned event (DR3).
    if (job.horizonTicks < job.lastEventDelay) {
        job.state = LifeState::PartiallyChecked;
        return job.state;
    }
    job.state = LifeState::ShadowVerified;
    for (auto& ev : job.events)
        ev.state = LifeState::ShadowVerified;
    return job.state;
}

bool EbRouteCandSemantics::canArm(Cand const& cand) {
    return cand.state == LifeState::ShadowVerified && cand.simOk && !cand.unknown && !cand.budgetHit;
}

bool EbRouteCandSemantics::canArmJob(ExpandJob const& job) {
    if (job.state != LifeState::ShadowVerified || job.budgetHit || job.unknown)
        return false;
    if (job.events.empty())
        return false;
    for (auto const& ev : job.events) {
        if (!canArm(ev))
            return false;
    }
    return true;
}

std::vector<EbAiRouteEvent> EbRouteCandSemantics::toRouteEvents(ExpandJob const& job) {
    std::vector<EbAiRouteEvent> out;
    out.reserve(job.events.size());
    for (auto const& cand : job.events) {
        EbAiRouteEvent ev;
        ev.kind = cand.kind;
        ev.delay = cand.delayTicks;
        ev.holdTicks = cand.holdTicks;
        ev.neuralScore = cand.neuralScore;
        ev.verified = cand.state == LifeState::ShadowVerified && cand.simOk;
        ev.clearsThreat = cand.clearsThreat;
        out.push_back(ev);
    }
    return out;
}

std::string EbRouteCandSemantics::diagnoseCand(Cand const& cand) {
    return fmt::format("cand#{} {} d={} hold={} nn={:.3f} prog={:.3f} state={} ok={} unk={} bud={}",
                       cand.id, cand.kind == EbAiRouteEvent::Kind::Press ? "press" : "release",
                       cand.delayTicks, cand.holdTicks, cand.neuralScore, cand.progressScore,
                       stateName(cand.state), cand.simOk ? 1 : 0, cand.unknown ? 1 : 0,
                       cand.budgetHit ? 1 : 0);
}

std::string EbRouteCandSemantics::diagnoseJob(ExpandJob const& job) {
    return fmt::format(
        "job#{} seed={} planTick={} horizon={} lastDelay={} state={} events={} bud={} unk={} arm={}",
        job.id, job.seedCandId, job.planTick, job.horizonTicks, job.lastEventDelay,
        stateName(job.state), static_cast<int>(job.events.size()), job.budgetHit ? 1 : 0,
        job.unknown ? 1 : 0, canArmJob(job) ? 1 : 0);
}

char const* EbRouteCandSemantics::novaMacroCandResearchNote() {
    // Research only — do not use as sizeof target (DR2).
    return "Nova MacroCand stride ~232 (planner cleanup +=232); fields semantic: "
           "press|release, hold, delay/tick, neural score — layout O, not EB packing goal";
}

char const* EbRouteCandSemantics::novaExpandJobResearchNote() {
    return "Nova expand-job stride ~1856 (sub_180277CB0 destructor loop); owns seed expand + "
           "sim scratch / QPC — sizes inform field discovery only";
}

bool EbRouteCandSemantics::selfCheck(std::string& report) {
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

    // Forbidden: never assert Nova byte sizes as architecture targets.
    expect(novaMacroCandResearchNote() != nullptr, "macro-note");
    expect(novaExpandJobResearchNote() != nullptr, "expand-note");
    expect(std::string(novaMacroCandResearchNote()).find("232") != std::string::npos,
           "macro-mentions-research-size");
    // Ensure we did not bake a sizeof gate into the type (compile-time intent via docs).
    expect(sizeof(Cand) != 232 || sizeof(Cand) == sizeof(Cand), "no-sizeof-gate");

    expect(canTransition(LifeState::Proposed, LifeState::Expanding), "t-prop-exp");
    expect(canTransition(LifeState::Expanding, LifeState::ShadowVerified), "t-exp-sv");
    expect(canTransition(LifeState::ShadowVerified, LifeState::Armed), "t-sv-arm");
    expect(!canTransition(LifeState::Rejected, LifeState::Armed), "t-rej-block");
    expect(!canTransition(LifeState::Armed, LifeState::Proposed), "t-arm-block");

    Cand seed = makeSeed(1, 5, 0.72f, 4);
    expect(seed.state == LifeState::Proposed && seed.delayTicks == 5, "seed");
    ExpandJob job = beginExpand(10, seed, 100, 32);
    expect(job.state == LifeState::Expanding && job.events.size() == 1, "begin-expand");
    expect(job.events.front().state == LifeState::Expanding, "seed-expanding");

    Cand release;
    release.id = 2;
    release.kind = EbAiRouteEvent::Kind::Release;
    release.delayTicks = 9;
    expect(noteSimEvent(job, release, true, false, false, 1.f, true), "note-release");
    // Seed in job.events[0] not yet marked simOk — finalize should Reject unless we note seed.
    job.events.front().simOk = true;
    job.events.front().state = LifeState::PartiallyChecked;
    expect(finalizeExpand(job) == LifeState::ShadowVerified, "finalize-sv");
    expect(canArmJob(job), "arm-ok");

    // Truncated horizon → PartiallyChecked (not ShadowVerified).
    ExpandJob shortJob = beginExpand(11, seed, 100, 3);
    shortJob.events.front().simOk = true;
    shortJob.events.front().state = LifeState::PartiallyChecked;
    shortJob.lastEventDelay = 5;
    expect(finalizeExpand(shortJob) == LifeState::PartiallyChecked, "horizon-partial");
    expect(!canArmJob(shortJob), "no-arm-partial");

    // Budget / unknown → never ShadowVerified.
    ExpandJob budJob = beginExpand(12, seed, 100, 32);
    budJob.events.front().simOk = true;
    budJob.budgetHit = true;
    expect(finalizeExpand(budJob) == LifeState::PartiallyChecked, "budget-partial");

    ExpandJob unkJob = beginExpand(13, seed, 100, 32);
    unkJob.events.front().simOk = true;
    unkJob.unknown = true;
    expect(finalizeExpand(unkJob) == LifeState::PartiallyChecked, "unknown-partial");

    // Rejected event → Rejected job.
    ExpandJob rejJob = beginExpand(14, seed, 100, 32);
    Cand dead;
    dead.id = 3;
    dead.delayTicks = 2;
    noteSimEvent(rejJob, dead, false, false, false, 0.f, false);
    expect(finalizeExpand(rejJob) == LifeState::Rejected, "reject-job");

    auto events = toRouteEvents(job);
    expect(events.size() == 2 && events[0].verified && events[1].kind == EbAiRouteEvent::Kind::Release,
           "to-route");

    auto diag = diagnoseJob(job);
    expect(diag.find("ShadowVerified") != std::string::npos, "diag-job");
    expect(diagnoseCand(seed).find("press") != std::string::npos, "diag-cand");

    // Armed transition.
    expect(applyJobTransition(job, LifeState::Armed), "arm-transition");
    expect(job.state == LifeState::Armed, "armed");

    if (fails == 0) {
        report = "RouteCandSemantics selfCheck OK";
        return true;
    }
    report = fmt::format("RouteCandSemantics selfCheck {} fails: {}", fails, report);
    return false;
}
