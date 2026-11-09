#include "../../app/class_manager.hpp"
#include "ghost_parity_probe.hpp"
#include "../../debug/assist_log.hpp"
#include "../../debug/lag_probe.hpp"
#include "../../debug/perf_trace.hpp"
#include "../learning/spot_trust_map.hpp"
#include "../replay/replay_import.hpp"
#include "../sim/shadow_simulator.hpp"
#include "../sim/sim_trust.hpp"
#include "assist_real_metrics.hpp"
#include <chrono>
#include <cmath>
#include <fmt/format.h>

EbGhostParityProbe& EbGhostParityProbe::get() {
    return EbClassManager::get().service<EbGhostParityProbe>();
}

void EbGhostParityProbe::setEps(float epsX, float epsY) {
    if (epsX > 0.f && std::isfinite(epsX))
        this->m_epsX = epsX;
    if (epsY > 0.f && std::isfinite(epsY))
        this->m_epsY = epsY;
}

void EbGhostParityProbe::resetAttempt() {
    this->m_poses.clear();
    this->m_anchors[0] = {};
    this->m_anchors[1] = {};
    this->m_nextAnchor = 0;
    this->m_lastDrifted = false;
    // Keep probe/drift counters across attempts for overlay diagnostics.
}

void EbGhostParityProbe::onLevelQuit() {
    this->resetAttempt();
    this->m_probeCount = 0;
    this->m_driftCount = 0;
}

void EbGhostParityProbe::recordPose(PlayerObject* player, int attemptTick) {
    if (!player)
        return;
    PoseSample s;
    s.tick = attemptTick;
    s.x = player->getPositionX();
    s.y = player->getPositionY();
    int const jumpIdx = static_cast<int>(PlayerButton::Jump);
    s.jumpHeld = player->m_holdingButtons[jumpIdx];
    s.dead = player->m_isDead;
    this->m_poses.push_back(s);
    if (static_cast<int>(this->m_poses.size()) > EbGhostParityProbe::kPoseCap) {
        this->m_poses.erase(this->m_poses.begin(),
                            this->m_poses.begin() + (static_cast<int>(this->m_poses.size()) -
                                                     EbGhostParityProbe::kPoseCap));
    }
}

EbGhostParityProbe::PoseSample const* EbGhostParityProbe::findPose(int tick) const {
    for (auto it = this->m_poses.rbegin(); it != this->m_poses.rend(); ++it) {
        if (it->tick == tick)
            return &(*it);
    }
    return nullptr;
}

void EbGhostParityProbe::maybeCaptureAnchor(PlayerObject* player, int attemptTick) {
    if (!player || player->m_isDead)
        return;
    if (attemptTick < 2)
        return;
    if (attemptTick % EbGhostParityProbe::kAnchorEvery != 0)
        return;

    int const which = this->m_nextAnchor & 1;
    this->m_nextAnchor = (this->m_nextAnchor + 1) & 1;
    int const slot = EbShadowSimulator::get().captureParitySnapshot(player, which);
    if (slot < 0)
        return;

    this->m_anchors[which].tick = attemptTick;
    this->m_anchors[which].slot = slot;
    this->m_anchors[which].valid = true;
}

void EbGhostParityProbe::maybeRunProbe(PlayLayer* layer, PlayerObject* player, float simDt,
                                     int attemptTick) {
    if (!layer || !player || player->m_isDead)
        return;
    if (EbShadowSimulator::get().isSimulating())
        return;
    if (attemptTick < EbGhostParityProbe::kHorizonTicks + EbGhostParityProbe::kAnchorEvery) {
        return;
    }
    if (attemptTick % EbGhostParityProbe::kProbeEvery != 0)
        return;
    if (!(simDt > 0.f) || !std::isfinite(simDt))
        return;

    // Prefer the older of the two anchors that still covers ≥ horizon/2.
    Anchor const* anchor = nullptr;
    int bestAge = -1;
    for (auto const& a : this->m_anchors) {
        if (!a.valid || a.slot < 0)
            continue;
        int const age = attemptTick - a.tick;
        if (age < EbGhostParityProbe::kHorizonTicks / 2)
            continue;
        if (age > EbGhostParityProbe::kPoseCap)
            continue;
        if (age > bestAge) {
            bestAge = age;
            anchor = &a;
        }
    }
    if (!anchor || bestAge <= 0)
        return;

    int const until = std::min(bestAge, EbGhostParityProbe::kHorizonTicks);
    std::vector<EbReplayEvent> events;
    events.reserve(static_cast<size_t>(until) / 4 + 2);

    bool prevHeld = false;
    if (auto const* startPose = this->findPose(anchor->tick)) {
        prevHeld = startPose->jumpHeld;
        if (prevHeld) {
            events.push_back(EbReplayEvent{0, 1, false, true});
        }
    }

    for (int tickValue = 1; tickValue <= until; ++tickValue) {
        auto const* pose = this->findPose(anchor->tick + tickValue);
        if (!pose)
            continue;
        if (pose->jumpHeld == prevHeld)
            continue;
        events.push_back(EbReplayEvent{tickValue, 1, false, pose->jumpHeld});
        prevHeld = pose->jumpHeld;
    }

    auto const livePose = this->findPose(anchor->tick + until);
    if (!livePose)
        return;

    EbPerfTrace::Scope const scope("ghostParity");
    auto const start = std::chrono::steady_clock::now();

    auto sample = EbShadowSimulator::get().samplePath(layer, anchor->slot, events, simDt, until);

    auto const elapsedMicroseconds =
        static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count());
    ++this->m_probeCount;

    if (elapsedMicroseconds > EbGhostParityProbe::kMaxProbeUs) {
        EbAssistLog::debug(
            fmt::format("parity: probe slow {}us (horizon={})", elapsedMicroseconds, until));
    }

    float ghostX = livePose->x;
    float ghostY = livePose->y;
    if (!sample.points.empty()) {
        ghostX = sample.points.back().x;
        ghostY = sample.points.back().y;
    }

    float const deltaX = std::fabs(ghostX - livePose->x);
    float const deltaY = std::fabs(ghostY - livePose->y);
    bool const deathMismatch = sample.died != livePose->dead;
    bool const drifted = deathMismatch || deltaX > this->m_epsX || deltaY > this->m_epsY;

    this->m_lastDrifted = drifted;
    EbAssistRealMetrics::get().noteParityProbe(drifted, deltaX, deltaY);
    if (!drifted) {
        EbSimTrust::get().clearParityDrift();
        EbSpotTrustMap::get().noteGoodParity(livePose->x);
        return;
    }

    ++this->m_driftCount;
    EbSimTrust::get().noteParityDrift();
    EbSpotTrustMap::get().markUntrusted(livePose->x);
    EbLagProbe::get().mark("eb.parity.drift",
                         fmt::format("dx={:.1f} dy={:.1f} deathMis={} age={} us={}", deltaX, deltaY,
                                     deathMismatch ? 1 : 0, until, elapsedMicroseconds));
    EbAssistLog::debug(fmt::format("parity DRIFT dx={:.1f} dy={:.1f} deathMis={} @x={:.0f}", deltaX,
                                 deltaY, deathMismatch ? 1 : 0, livePose->x));
}

void EbGhostParityProbe::onPhysicsTick(PlayLayer* layer, PlayerObject* player, float simDt,
                                     int attemptTick) {
    if (!layer || !player)
        return;
    this->recordPose(player, attemptTick);
    this->maybeCaptureAnchor(player, attemptTick);
    this->maybeRunProbe(layer, player, simDt, attemptTick);
}

bool EbGhostParityProbe::selfCheck(std::string& report) {
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

    EbGhostParityProbe& probe = EbGhostParityProbe::get();
    probe.onLevelQuit();
    expect(probe.probeCount() == 0, "quit-reset-probes");
    expect(probe.epsX() == EbGhostParityProbe::kDefaultEpsX, "default-eps-x");
    expect(probe.epsY() == EbGhostParityProbe::kDefaultEpsY, "default-eps-y");

    probe.setEps(5.f, 7.f);
    expect(probe.epsX() == 5.f, "set-eps-x");
    expect(probe.epsY() == 7.f, "set-eps-y");
    probe.setEps(-1.f, 0.f); // ignored
    expect(probe.epsX() == 5.f, "eps-reject-neg");

    probe.resetAttempt();
    expect(!probe.lastProbeDrifted(), "reset-no-drift");

    // Restore defaults for play.
    probe.setEps(EbGhostParityProbe::kDefaultEpsX, EbGhostParityProbe::kDefaultEpsY);
    probe.onLevelQuit();

    if (fails == 0) {
        report = "GhostParityProbe selfCheck OK";
        return true;
    }
    report = fmt::format("GhostParityProbe selfCheck {} fails: {}", fails, report);
    return false;
}
