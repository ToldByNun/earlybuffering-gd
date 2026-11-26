#include "route_expand_core.hpp"
#include <algorithm>
#include <fmt/format.h>

int EbRouteExpandCore::OccupancyRing::slot(int absTick) {
    if (absTick < 0)
        absTick = 0;
    return absTick % kBits;
}

bool EbRouteExpandCore::OccupancyRing::tryClaim(int absTick) {
    int const index = slot(absTick);
    if (m_bits[index])
        return false;
    m_bits[index] = true;
    ++m_count;
    return true;
}

void EbRouteExpandCore::OccupancyRing::release(int absTick) {
    int const index = slot(absTick);
    if (!m_bits[index])
        return;
    m_bits[index] = false;
    if (m_count > 0)
        --m_count;
}

bool EbRouteExpandCore::OccupancyRing::claimed(int absTick) const {
    return m_bits[slot(absTick)];
}

void EbRouteExpandCore::OccupancyRing::clear() {
    for (int index = 0; index < kBits; ++index)
        m_bits[index] = false;
    m_count = 0;
}

bool EbRouteExpandCore::budgetExhausted(Options const& opts, int elapsedUsApprox,
                                        bool outcomeBudgetHit) {
    if (outcomeBudgetHit)
        return true;
    if (opts.budgetUs <= 0)
        return false;
    return elapsedUsApprox >= opts.budgetUs;
}

EbRouteExpandCore::Result
EbRouteExpandCore::expandAroundSeed(std::uint32_t jobId, EbRouteCandSemantics::Cand seed,
                                    int planTick, Options const& opts, OccupancyRing& ring,
                                    SimProbe const& probe) {
    Result out;
    out.reason = "fail";
    if (!probe) {
        out.reason = "no-probe";
        return out;
    }

    int const horizon = std::max(0, opts.horizonTicks);
    out.job = EbRouteCandSemantics::beginExpand(jobId, seed, planTick, horizon);
    // beginExpand already inserted the seed — reset and re-sim so survival is explicit.
    out.job.events.clear();
    out.job.lastEventDelay = 0;

    int const pressAbs = planTick + std::max(0, seed.delayTicks);
    if (!ring.tryClaim(pressAbs)) {
        out.job.state = EbRouteCandSemantics::LifeState::Rejected;
        out.reason = "occupancy";
        return out;
    }

    SimOutcome pressOut = probe(EbAiRouteEvent::Kind::Press, seed.delayTicks, seed.holdTicks, 0);
    ++out.simCalls;
    EbRouteCandSemantics::Cand press = seed;
    press.kind = EbAiRouteEvent::Kind::Press;
    press.expandJobId = jobId;
    if (!EbRouteCandSemantics::noteSimEvent(out.job, press, pressOut.ok, pressOut.unknown,
                                            pressOut.budgetHit, pressOut.progress,
                                            pressOut.clearsThreat)) {
        ring.release(pressAbs);
        out.reason = "note-press";
        return out;
    }
    // noteSimEvent pushes; remove the Expanding placeholder if beginExpand left one — already cleared.

    if (budgetExhausted(opts, out.simCalls * 200, pressOut.budgetHit)) {
        out.truncated = true;
        out.job.budgetHit = true;
        EbRouteCandSemantics::finalizeExpand(out.job);
        out.reason = "budget-press";
        return out;
    }
    if (pressOut.unknown) {
        out.job.unknown = true;
        EbRouteCandSemantics::finalizeExpand(out.job);
        out.reason = "unknown-press";
        return out;
    }
    if (!pressOut.ok) {
        EbRouteCandSemantics::finalizeExpand(out.job);
        out.reason = "dead-press";
        return out;
    }

    if (opts.generateRelease && !opts.holdChoices.empty()) {
        for (int hold : opts.holdChoices) {
            if (out.simCalls >= opts.maxSimCalls ||
                static_cast<int>(out.job.events.size()) >= opts.maxEvents) {
                out.truncated = true;
                break;
            }
            if (hold <= 0)
                continue;
            int const releaseDelay = seed.delayTicks + hold;
            if (releaseDelay > horizon)
                continue;
            int const releaseAbs = planTick + releaseDelay;
            if (!ring.tryClaim(releaseAbs))
                continue;

            SimOutcome relOut =
                probe(EbAiRouteEvent::Kind::Release, releaseDelay, 0, out.simCalls);
            ++out.simCalls;
            EbRouteCandSemantics::Cand release;
            release.id = seed.id + 1000u + static_cast<std::uint32_t>(hold);
            release.kind = EbAiRouteEvent::Kind::Release;
            release.delayTicks = releaseDelay;
            release.neuralScore = seed.neuralScore;
            if (!EbRouteCandSemantics::noteSimEvent(out.job, release, relOut.ok, relOut.unknown,
                                                    relOut.budgetHit, relOut.progress,
                                                    relOut.clearsThreat)) {
                ring.release(releaseAbs);
                continue;
            }
            if (budgetExhausted(opts, out.simCalls * 200, relOut.budgetHit)) {
                out.truncated = true;
                out.job.budgetHit = true;
                break;
            }
            if (relOut.unknown) {
                out.job.unknown = true;
                out.truncated = true;
                break;
            }
            // Keep first surviving release; further holds are optional growth.
            if (relOut.ok)
                break;
            // Failed release — free occupancy so another hold can claim.
            ring.release(releaseAbs);
            if (!out.job.events.empty() &&
                out.job.events.back().kind == EbAiRouteEvent::Kind::Release)
                out.job.events.pop_back();
        }
    }

    auto const life = EbRouteCandSemantics::finalizeExpand(out.job);
    if (out.truncated && life == EbRouteCandSemantics::LifeState::ShadowVerified) {
        // Belt: truncated expand must never stay ShadowVerified.
        out.job.budgetHit = true;
        out.job.state = EbRouteCandSemantics::LifeState::PartiallyChecked;
        for (auto& ev : out.job.events) {
            if (ev.state == EbRouteCandSemantics::LifeState::ShadowVerified)
                ev.state = EbRouteCandSemantics::LifeState::PartiallyChecked;
        }
    }
    out.reason = honestyOk(out) ? (out.truncated ? "partial-trunc" : "ok") : "dishonest";
    if (out.job.state == EbRouteCandSemantics::LifeState::ShadowVerified)
        out.reason = "verified";
    else if (out.job.state == EbRouteCandSemantics::LifeState::PartiallyChecked)
        out.reason = out.truncated ? "partial-trunc" : "partial";
    else if (out.job.state == EbRouteCandSemantics::LifeState::Rejected)
        out.reason = "rejected";
    return out;
}

EbRouteExpandCore::Result
EbRouteExpandCore::stuckSimCoreProbe(std::uint32_t jobId, int planTick, int delayTicks,
                                     Options const& opts, OccupancyRing& ring,
                                     SimProbe const& probe) {
    Result out;
    out.stuckProbeOnly = true;
    out.reason = "stuck-fail";
    if (!probe) {
        out.reason = "no-probe";
        return out;
    }
    auto seed = EbRouteCandSemantics::makeSeed(jobId, delayTicks, 0.f, 0);
    // Neural score stays 0 — sim-core path only.
    Options stuckOpts = opts;
    stuckOpts.generateRelease = false;
    stuckOpts.maxEvents = 1;
    stuckOpts.maxSimCalls = 1;
    out = expandAroundSeed(jobId, seed, planTick, stuckOpts, ring, probe);
    out.stuckProbeOnly = true;
    if (out.reason && std::string(out.reason) == "verified")
        out.reason = "stuck-verified";
    else if (out.job.state == EbRouteCandSemantics::LifeState::PartiallyChecked)
        out.reason = "stuck-partial";
    else if (out.job.state == EbRouteCandSemantics::LifeState::Rejected)
        out.reason = "stuck-rejected";
    return out;
}

bool EbRouteExpandCore::honestyOk(Result const& result) {
    auto const life = result.job.state;
    if (result.truncated || result.job.budgetHit || result.job.unknown) {
        if (life == EbRouteCandSemantics::LifeState::ShadowVerified)
            return false;
        if (EbRouteCandSemantics::canArmJob(result.job))
            return false;
        return life == EbRouteCandSemantics::LifeState::PartiallyChecked ||
               life == EbRouteCandSemantics::LifeState::Rejected ||
               life == EbRouteCandSemantics::LifeState::Expanding ||
               life == EbRouteCandSemantics::LifeState::Proposed;
    }
    if (life == EbRouteCandSemantics::LifeState::ShadowVerified)
        return EbRouteCandSemantics::canArmJob(result.job);
    return true;
}

bool EbRouteExpandCore::selfCheck(std::string& report) {
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

    OccupancyRing ring;
    expect(ring.tryClaim(100), "claim-100");
    expect(ring.claimed(100), "claimed-100");
    expect(!ring.tryClaim(100), "double-claim");
    expect(ring.claimedCount() == 1, "count-1");
    ring.release(100);
    expect(!ring.claimed(100) && ring.tryClaim(100), "reclaim");
    ring.clear();
    expect(ring.claimedCount() == 0, "clear");

    Options opts;
    opts.budgetUs = 4000;
    opts.horizonTicks = 32;
    opts.maxSimCalls = 8;
    opts.holdChoices = {4, 8};

    // Full survival → ShadowVerified.
    {
        OccupancyRing r;
        auto seed = EbRouteCandSemantics::makeSeed(1, 2, 0.8f, 4);
        auto result = expandAroundSeed(
            10, seed, 100, opts, r,
            [](EbAiRouteEvent::Kind, int, int, int) {
                SimOutcome o;
                o.ok = true;
                o.progress = 1.f;
                o.clearsThreat = true;
                return o;
            });
        expect(result.job.state == EbRouteCandSemantics::LifeState::ShadowVerified, "full-sv");
        expect(honestyOk(result), "full-honest");
        expect(EbRouteCandSemantics::canArmJob(result.job), "full-arm");
        expect(r.claimed(102), "occ-press");
        expect(r.claimed(106), "occ-release"); // 100+2+4
    }

    // Truncated by budget mid-expand → not ShadowVerified.
    {
        OccupancyRing r;
        auto seed = EbRouteCandSemantics::makeSeed(2, 1, 0.5f, 4);
        Options tight = opts;
        tight.budgetUs = 100; // simCalls*200 trips after first call
        auto result = expandAroundSeed(
            11, seed, 50, tight, r,
            [](EbAiRouteEvent::Kind, int, int, int) {
                SimOutcome o;
                o.ok = true;
                o.budgetHit = true;
                o.progress = 0.5f;
                return o;
            });
        expect(result.truncated || result.job.budgetHit, "trunc-flag");
        expect(result.job.state != EbRouteCandSemantics::LifeState::ShadowVerified, "trunc-not-sv");
        expect(!EbRouteCandSemantics::canArmJob(result.job), "trunc-no-arm");
        expect(honestyOk(result), "trunc-honest");
    }

    // Unknown must not promote.
    {
        OccupancyRing r;
        auto seed = EbRouteCandSemantics::makeSeed(3, 0, 0.9f, 0);
        Options noRel = opts;
        noRel.generateRelease = false;
        auto result = expandAroundSeed(
            12, seed, 0, noRel, r,
            [](EbAiRouteEvent::Kind, int, int, int) {
                SimOutcome o;
                o.ok = true;
                o.unknown = true;
                return o;
            });
        expect(result.job.state != EbRouteCandSemantics::LifeState::ShadowVerified, "unk-not-sv");
        expect(honestyOk(result), "unk-honest");
    }

    // Occupancy blocks second seed on same tick.
    {
        OccupancyRing r;
        Options noRel = opts;
        noRel.generateRelease = false;
        auto seedA = EbRouteCandSemantics::makeSeed(4, 3, 0.7f, 0);
        auto a = expandAroundSeed(
            13, seedA, 200, noRel, r,
            [](EbAiRouteEvent::Kind, int, int, int) {
                SimOutcome o;
                o.ok = true;
                return o;
            });
        expect(a.job.state == EbRouteCandSemantics::LifeState::ShadowVerified, "occ-first");
        auto seedB = EbRouteCandSemantics::makeSeed(5, 3, 0.7f, 0);
        auto b = expandAroundSeed(
            14, seedB, 200, noRel, r,
            [](EbAiRouteEvent::Kind, int, int, int) {
                SimOutcome o;
                o.ok = true;
                return o;
            });
        expect(std::string(b.reason) == "occupancy", "occ-block");
    }

    // Stuck/memory sim-core: neural score ignored (0), single probe.
    {
        OccupancyRing r;
        auto stuck = stuckSimCoreProbe(
            15, 10, 2, opts, r,
            [](EbAiRouteEvent::Kind kind, int, int, int) {
                SimOutcome o;
                o.ok = kind == EbAiRouteEvent::Kind::Press;
                o.progress = 0.4f;
                return o;
            });
        expect(stuck.stuckProbeOnly, "stuck-flag");
        expect(stuck.simCalls == 1, "stuck-one-sim");
        expect(stuck.job.events.size() == 1, "stuck-one-event");
        expect(stuck.job.events.front().neuralScore == 0.f, "stuck-no-neural");
    }

    // Dead press → Rejected, honesty ok.
    {
        OccupancyRing r;
        Options noRel = opts;
        noRel.generateRelease = false;
        auto seed = EbRouteCandSemantics::makeSeed(6, 1, 0.2f, 0);
        auto result = expandAroundSeed(
            16, seed, 0, noRel, r,
            [](EbAiRouteEvent::Kind, int, int, int) {
                SimOutcome o;
                o.ok = false;
                return o;
            });
        expect(result.job.state == EbRouteCandSemantics::LifeState::Rejected, "dead-rej");
        expect(honestyOk(result), "dead-honest");
    }

    if (fails == 0) {
        report = "RouteExpandCore selfCheck OK";
        return true;
    }
    report = fmt::format("RouteExpandCore selfCheck {} fails: {}", fails, report);
    return false;
}
