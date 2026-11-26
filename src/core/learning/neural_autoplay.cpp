#include "neural_autoplay.hpp"
#include "../../app/class_manager.hpp"
#include "../../config/stage_config.hpp"
#include "../../debug/assist_log.hpp"
#include "../assist/assist_controller.hpp"
#include "../assist/contracts/hook_orchestration.hpp"
#include "../sim/shadow_simulator.hpp"
#include "ai_config.hpp"
#include "ai_route_planner.hpp"
#include "autoplay_metrics.hpp"
#include "neural_learning.hpp"
#include "shadow_verified_contract.hpp"
#include <Geode/Geode.hpp>
#include <cstdlib>

#if defined(EB_DESKTOP_IMGUI)
#include "../../ui/desktop_menu/desktop_menu.hpp"
#endif

EbNeuralAutoplay& EbNeuralAutoplay::get() {
    return EbClassManager::get().service<EbNeuralAutoplay>();
}

void EbNeuralAutoplay::syncFromConfig() {
    auto const& cfg = EbAiConfig::get();
    m_restartOnDeath = cfg.restartOnDeath;
    m_timeWarp = cfg.timeWarp;
    m_exploreRate = cfg.exploreRate;
    m_probeEveryTicks = cfg.probeEveryTicks;
    m_gapTicks = cfg.gapTicks;
    m_maxRouteQueue = cfg.maxRouteQueue;
    m_earlyPlanTicks = cfg.earlyPlanTicks;
    m_maxAutoplayCps = cfg.maxAutoplayCps;
    m_memoryReuseRoute = cfg.memoryReuseRoute;
    m_memoryHoldTicks = cfg.memoryHoldTicks;
    if (m_enabled)
        m_warpNeedsApply = true;
    if (cfg.autoplay != m_enabled)
        setEnabled(cfg.autoplay);
}

void EbNeuralAutoplay::syncTimeWarp(PlayLayer* layer) {
    if (m_warpNeedsApply) {
        m_warpNeedsApply = false;
        applyTimeWarp(layer);
    }
    if (m_warpNeedsRestore) {
        m_warpNeedsRestore = false;
        restoreTimeWarp(layer);
    }
}

void EbNeuralAutoplay::applyTimeWarp(PlayLayer* layer) {
    if (!layer)
        return;
    if (!m_warpOwned) {
        m_savedTimeWarp = layer->m_gameState.m_timeWarp;
        if (m_savedTimeWarp <= 0.f)
            m_savedTimeWarp = 1.f;
        m_warpOwned = true;
    }
    layer->m_gameState.m_timeWarp = m_timeWarp;
}

void EbNeuralAutoplay::restoreTimeWarp(PlayLayer* layer) {
    if (!m_warpOwned)
        return;
    if (layer)
        layer->m_gameState.m_timeWarp = m_savedTimeWarp > 0.f ? m_savedTimeWarp : 1.f;
    m_warpOwned = false;
}

void EbNeuralAutoplay::stickTimeWarp(PlayLayer* layer) {
    if (!m_enabled || !m_warpOwned || !layer)
        return;
    if (layer->m_gameState.m_timeWarp < m_timeWarp * 0.5f)
        layer->m_gameState.m_timeWarp = m_timeWarp;
}

void EbNeuralAutoplay::onAttemptReset() {
    m_holding = false;
    m_holdUntilTick = -1;
    m_pendingRestart = false;
    m_restartAtTick = -1;
    m_nextPressTick = 0;
    m_nextProbeTick = 8;
    m_memoryUntilTick = -1;
    m_memoryRoute.clear();
    m_routeQueue.clear();
    m_recentPressTicks.clear();
    m_lastSeedNova = 0;
    m_lastSeedEnh = 0;
    m_lastSeedPost = 0;
    clearSchedule();
    clearRoute();
    // Keep session metrics across attempts; only clear route state.
    EbAiRoutePlanner::get().invalidate("attempt-reset");
    if (m_enabled)
        stickTimeWarp(PlayLayer::get());
}

int EbNeuralAutoplay::holdTicksForPlayer(PlayLayer* layer) const {
    if (!layer || !layer->m_player1)
        return 4;
    switch (EbStageConfig::kindOf(layer->m_player1)) {
    case GameModeKind::Ship:
    case GameModeKind::Swing:
        return 10;
    case GameModeKind::Wave:
        return 3;
    case GameModeKind::Ufo:
    case GameModeKind::Robot:
        return 5;
    default:
        return 4;
    }
}

bool EbNeuralAutoplay::cpsAllowsPress(int tick) const {
    int const window = 240;
    int const maxCps = std::max(1, m_maxAutoplayCps);
    int count = 0;
    for (int t : m_recentPressTicks) {
        if (tick - t < window)
            ++count;
    }
    return count < maxCps;
}

void EbNeuralAutoplay::recordPressTick(int tick) {
    m_recentPressTicks.push_back(tick);
    while (!m_recentPressTicks.empty() && tick - m_recentPressTicks.front() >= 240)
        m_recentPressTicks.pop_front();
}

void EbNeuralAutoplay::beginVerifiedRoute(EbAiRoute const& route, float startX, int tick) {
    m_activeRoute = route;
    m_verifiedSnapshot = route;
    m_executedEvents.clear();
    m_routeIndex = 0;
    m_routeStartX = startX;
    m_routeStartTick = tick;
    m_memoryRoute = route;
    m_memoryUntilTick = tick + std::max(4, m_memoryHoldTicks);
}

void EbNeuralAutoplay::finishRouteSegment(float liveX, int tick, bool death) {
    if (m_verifiedSnapshot.events.empty())
        return;
    bool const match =
        EbShadowVerifiedContract::sequencesMatch(m_verifiedSnapshot.events, m_executedEvents);
    m_metrics.noteFidelity(match);
    float const dx = liveX - m_routeStartX;
    int const survived =
        m_routeStartTick >= 0 ? std::max(0, tick - m_routeStartTick) : 0;
    if (!death || dx > 0.f)
        m_metrics.noteProgress(std::max(0.f, dx), survived);
    m_verifiedSnapshot.clear();
    m_executedEvents.clear();
    m_routeStartTick = -1;
}

void EbNeuralAutoplay::press(PlayLayer* layer, bool explore) {
    if (!layer)
        return;
    auto& controller = EbAssistController::get();
    int const tick = controller.attemptTick();
    if (!explore && !cpsAllowsPress(tick)) {
        m_metrics.noteCpsThrottle();
        m_lastProbeReason = "cps-throttle";
        m_scheduledPressTick = tick + std::max(1, 240 / std::max(1, m_maxAutoplayCps));
        return;
    }
    // AP-NL7: single inject flag — Autoplay must not re-enter Live planning.
    EbHookOrchestration::InjectGuard inj;
    if (!inj) {
        m_lastProbeReason = "inject-reentry";
        return;
    }
    int const jump = EbAssistControllerImplementation::JUMP_BUTTON;
    controller.m_injecting = true;
    layer->handleButton(true, jump, true);
    controller.m_injecting = false;
    m_holding = true;
    m_holdUntilTick = -1;
    m_plannedHoldTicks = -1;
    ++m_presses;
    recordPressTick(tick);
    if (explore)
        ++m_explorePresses;
    // Record executed press as the verified sequence entry (order + relative delay).
    if (!explore && !m_activeRoute.events.empty() && m_routeIndex > 0) {
        int const idx = m_routeIndex - 1;
        if (idx >= 0 && idx < static_cast<int>(m_activeRoute.events.size())) {
            auto const& planned = m_activeRoute.events[static_cast<size_t>(idx)];
            if (planned.kind == EbAiRouteEvent::Kind::Press)
                m_executedEvents.push_back(planned);
        }
    }
    // If the route has an explicit Release next, schedule it; else fall back to hold ticks.
    if (!scheduleNextRouteEvent(layer) && m_holding) {
        int const hold = holdTicksForPlayer(layer);
        m_holdUntilTick = tick + hold;
    }
}

void EbNeuralAutoplay::release(PlayLayer* layer) {
    if (!layer)
        return;
    auto& controller = EbAssistController::get();
    int const tick = controller.attemptTick();
    EbHookOrchestration::InjectGuard inj;
    if (!inj) {
        m_lastProbeReason = "inject-reentry";
        return;
    }
    int const jump = EbAssistControllerImplementation::JUMP_BUTTON;
    controller.m_injecting = true;
    layer->handleButton(false, jump, true);
    controller.m_injecting = false;
    m_holding = false;
    m_holdUntilTick = -1;
    m_scheduledReleaseTick = -1;
    // Record executed release as the verified sequence entry.
    if (!m_activeRoute.events.empty() && m_routeIndex > 0) {
        int const idx = m_routeIndex - 1;
        if (idx >= 0 && idx < static_cast<int>(m_activeRoute.events.size())) {
            auto const& planned = m_activeRoute.events[static_cast<size_t>(idx)];
            if (planned.kind == EbAiRouteEvent::Kind::Release)
                m_executedEvents.push_back(planned);
        }
    }
    m_nextPressTick = tick + m_gapTicks;
    m_nextProbeTick = m_nextPressTick;
    if (!scheduleNextRouteEvent(layer)) {
        float const liveX = layer->m_player1 ? layer->m_player1->getPositionX() : m_routeStartX;
        finishRouteSegment(liveX, tick, false);
        clearRoute();
        // Promote queued early-plan route.
        if (!m_routeQueue.empty()) {
            auto next = std::move(m_routeQueue.front());
            m_routeQueue.pop_front();
            beginVerifiedRoute(next, liveX, tick);
            m_metrics.noteQueueArm();
            m_lastProbeReason = "queue-promote";
            (void)scheduleNextRouteEvent(layer);
        }
    }
}

bool EbNeuralAutoplay::scheduleNextRouteEvent(PlayLayer* layer) {
    if (!layer || m_activeRoute.events.empty())
        return false;
    if (m_routeIndex >= static_cast<int>(m_activeRoute.events.size())) {
        return false;
    }
    auto const& ev = m_activeRoute.events[static_cast<size_t>(m_routeIndex)];
    ++m_routeIndex;
    int const tick = EbAssistController::get().attemptTick();
    int const absTick = m_activeRoute.planTick + std::max(0, ev.delay);
    if (ev.kind == EbAiRouteEvent::Kind::Press) {
        m_plannedHoldTicks = ev.holdTicks > 0 ? ev.holdTicks : holdTicksForPlayer(layer);
        m_scheduledExplore = false;
        m_scheduledReleaseTick = -1;
        if (absTick <= tick) {
            m_scheduledPressTick = tick;
        } else {
            m_scheduledPressTick = absTick;
        }
        m_lastProbeReason = ev.verified ? "route-press" : "route-press-soft";
        m_nextProbeTick = m_scheduledPressTick + 1;
        return true;
    }
    // Release is a first-class scheduled event (not only holdUntilTick).
    m_scheduledPressTick = -1;
    m_plannedHoldTicks = -1;
    if (absTick <= tick) {
        m_scheduledReleaseTick = tick;
    } else {
        m_scheduledReleaseTick = absTick;
    }
    m_holdUntilTick = m_scheduledReleaseTick;
    m_lastProbeReason = ev.verified ? "route-release" : "route-release-soft";
    m_nextProbeTick = m_scheduledReleaseTick + 1;
    return true;
}

bool EbNeuralAutoplay::tryEarlyPlan(PlayLayer* layer) {
    if (!layer || !layer->m_player1 || m_maxRouteQueue <= 0 || m_earlyPlanTicks <= 0)
        return false;
    if (static_cast<int>(m_routeQueue.size()) >= m_maxRouteQueue)
        return false;
    if (m_activeRoute.events.empty() || m_routeIndex >= static_cast<int>(m_activeRoute.events.size()))
        return false;
    // Remaining delay until last committed event.
    int const lastDelay = m_activeRoute.events.back().delay;
    int const tick = EbAssistController::get().attemptTick();
    int const remaining = (m_activeRoute.planTick + lastDelay) - tick;
    if (remaining > m_earlyPlanTicks)
        return false;

    auto& controller = EbAssistController::get();
    auto& neural = EbNeuralLearning::get();
    if (!EbNeuralLearning::active() || !neural.loaded())
        return false;
    auto* player = layer->m_player1;
    int const limit = std::max(1, controller.activeLimit(player));
    float const dt = controller.simDt() > 0.f ? controller.simDt() : (1.f / 240.f);
    EbAiRoute route =
        EbAiRoutePlanner::get().plan(layer, player, true, tick, limit, dt);
    auto const& job = EbAiRoutePlanner::get().lastExpandJob();
    if (!route.valid || route.events.empty() ||
        !EbShadowVerifiedContract::mayArmAutoplay(job, false))
        return false;
    if (!EbShadowVerifiedContract::sequencesMatch(EbRouteCandSemantics::toRouteEvents(job),
                                                  route.events))
        return false;
    m_routeQueue.push_back(route);
    m_metrics.noteVerifyComplete();
    m_lastProbeReason = "early-plan-queued";
    return true;
}

bool EbNeuralAutoplay::armFromRoute(PlayLayer* layer, bool& exploreOut) {
    exploreOut = false;
    m_plannedHoldTicks = -1;
    m_lastProbeReason = "skip";
    if (!layer || !layer->m_player1)
        return false;
    auto* player = layer->m_player1;
    auto& controller = EbAssistController::get();
    auto& neural = EbNeuralLearning::get();
    auto& cfg = EbAiConfig::get();
    if (!EbNeuralLearning::active() || !neural.loaded()) {
        m_lastProbeReason = "not-loaded";
        m_metrics.noteVerifyOther();
        return false;
    }
    ++m_probes;

    int const limit = std::max(1, controller.activeLimit(player));
    float const dt = controller.simDt() > 0.f ? controller.simDt() : (1.f / 240.f);
    int const tick = controller.attemptTick();
    float const liveX = player->getPositionX();

    auto& planner = EbAiRoutePlanner::get();
    // Seed diagnostics (deltas since last probe) — not survival.
    auto const& seeds = planner.seedCounters;
    m_metrics.noteSeedDiag(seeds.novaParityAccepts - m_lastSeedNova,
                           seeds.enhancedProposes - m_lastSeedEnh,
                           seeds.postVerifyArms - m_lastSeedPost);
    m_lastSeedNova = seeds.novaParityAccepts;
    m_lastSeedEnh = seeds.enhancedProposes;
    m_lastSeedPost = seeds.postVerifyArms;

    if (cfg.replanOnDivergence && planner.needsReplan(tick, liveX))
        planner.invalidate("divergence");

    // Promote queued route before a fresh plan when idle.
    if (!m_routeQueue.empty() && m_activeRoute.events.empty()) {
        auto next = std::move(m_routeQueue.front());
        m_routeQueue.pop_front();
        beginVerifiedRoute(next, liveX, tick);
        m_metrics.noteQueueArm();
        m_metrics.noteVerifyComplete();
        m_scheduledExplore = false;
        if (!scheduleNextRouteEvent(layer)) {
            clearRoute();
            m_lastProbeReason = "no-event";
            return false;
        }
        m_lastProbeReason = "queue-arm";
        return true;
    }

    EbAiRoute route = planner.plan(layer, player, true, tick, limit, dt);
    auto const& job = planner.lastExpandJob();
    bool const shadowOk = route.valid && !route.events.empty() &&
                          EbShadowVerifiedContract::mayArmAutoplay(job, false) &&
                          EbShadowVerifiedContract::sequencesMatch(
                              EbRouteCandSemantics::toRouteEvents(job), route.events);

    if (shadowOk) {
        if (route.reason && std::string(route.reason) == "shadow-verified" &&
            planner.lastExpandResult().stuckProbeOnly) {
            m_metrics.noteStuckRecovery();
            m_lastProbeReason = "stuck-sim-core";
        }
        m_metrics.noteVerifyComplete();
        beginVerifiedRoute(route, liveX, tick);
        m_scheduledExplore = false;
        if (!scheduleNextRouteEvent(layer)) {
            clearRoute();
            m_lastProbeReason = "no-event";
            return false;
        }
        m_lastProbeReason = "shadow-verified";
        return true;
    }

    // Memory reuse of last SV route (Autoplay-only; does not touch Live).
    if (m_memoryReuseRoute && m_memoryUntilTick >= tick && !m_memoryRoute.events.empty()) {
        beginVerifiedRoute(m_memoryRoute, liveX, tick);
        m_metrics.noteMemoryReuse();
        m_metrics.noteVerifyComplete();
        m_scheduledExplore = false;
        if (scheduleNextRouteEvent(layer)) {
            m_lastProbeReason = "memory-reuse";
            return true;
        }
        clearRoute();
    }

    if (route.budgetHit)
        m_metrics.noteVerifyBudget();
    else if (route.reason && (std::string(route.reason) == "partial-verify" ||
                              std::string(route.reason) == "prefix-short"))
        m_metrics.noteVerifyPartial();
    else
        m_metrics.noteVerifyOther();

    if (m_exploreRate > 0.f) {
        float const roll = static_cast<float>(std::rand() % 10000) / 10000.f;
        if (roll < m_exploreRate) {
            exploreOut = true;
            m_scheduledExplore = true;
            m_plannedHoldTicks = holdTicksForPlayer(layer);
            m_lastProbeReason = "explore-unverified";
            clearRoute();
            m_scheduledPressTick = tick;
            m_scheduledReleaseTick = -1;
            return true;
        }
    }
    m_lastProbeReason = route.reason ? route.reason : "no-shadow-verified";
    return false;
}

void EbNeuralAutoplay::onDeath(PlayLayer* layer) {
    if (!m_enabled)
        return;
    int const tick = EbAssistController::get().attemptTick();
    float const liveX =
        layer && layer->m_player1 ? layer->m_player1->getPositionX() : m_routeStartX;
    finishRouteSegment(liveX, tick, true);
    m_holding = false;
    m_holdUntilTick = -1;
    m_routeQueue.clear();
    m_memoryUntilTick = -1;
    m_memoryRoute.clear();
    clearSchedule();
    clearRoute();
    EbAiRoutePlanner::get().invalidate("death");
    if (!m_restartOnDeath || !layer)
        return;
    m_pendingRestart = true;
    m_restartAtTick = tick + 30;
}

void EbNeuralAutoplay::onPhysicsTick(PlayLayer* layer) {
    if (!layer)
        return;

    syncTimeWarp(layer);

    if (!m_enabled)
        return;
    if (EbStageConfig::assistMode() != EbStageConfig::AssistMode::AI)
        return;
    if (!EbStageConfig::isEnabled())
        return;
    if (EbShadowSimulator::get().isSimulating())
        return;
    if (layer->m_isPaused)
        return;
#if defined(EB_DESKTOP_IMGUI)
    if (EbDesktopMenu::isOpen())
        return;
#endif

    stickTimeWarp(layer);

    auto& controller = EbAssistController::get();
    int const tick = controller.attemptTick();

    if (m_pendingRestart && m_restartAtTick >= 0 && tick >= m_restartAtTick) {
        m_pendingRestart = false;
        m_restartAtTick = -1;
        ++m_restarts;
        EbAssistLog::info("AI autoplay: restart attempt");
        layer->resetLevel();
        return;
    }

    if (!layer->m_player1 || layer->m_player1->m_isDead)
        return;

    // AP-NL7: platformer hard-off — never schedule or emit Autoplay inputs.
    if (EbHookOrchestration::platformerHardOff(layer->m_player1)) {
        m_lastProbeReason = "platformer-off";
        clearSchedule();
        clearRoute();
        m_routeQueue.clear();
        return;
    }
    // Autoplay only in PrePhysicsAssist (before PlayerObject::update).
    if (EbHookOrchestration::phase() != EbHookOrchestration::Phase::PrePhysicsAssist)
        return;

    // Early-plan while a verified route is still running (Autoplay queue only).
    if (!m_activeRoute.events.empty() && m_holding && !EbHookOrchestration::planning() &&
        !EbHookOrchestration::injecting())
        (void)tryEarlyPlan(layer);

    // Explicit Release from the route scheduler (preferred over holdUntilTick alone).
    if (m_scheduledReleaseTick >= 0 && tick >= m_scheduledReleaseTick) {
        m_scheduledReleaseTick = -1;
        release(layer);
        return;
    }

    if (m_holding) {
        if (m_holdUntilTick >= 0 && tick >= m_holdUntilTick)
            release(layer);
        return;
    }

    if (m_scheduledPressTick >= 0) {
        if (tick < m_scheduledPressTick)
            return;
        if (controller.buffer().state != BufferState::Idle || controller.userHolding(true)) {
            m_scheduledPressTick = tick + 1;
            return;
        }
        bool const explore = m_scheduledExplore;
        m_scheduledPressTick = -1;
        m_scheduledExplore = false;
        press(layer, explore);
        return;
    }

    if (tick < m_nextPressTick)
        return;
    if (controller.buffer().state != BufferState::Idle)
        return;
    if (controller.userHolding(true))
        return;
    if (tick < 8)
        return;
    if (tick < m_nextProbeTick)
        return;

    m_nextProbeTick = tick + m_probeEveryTicks;
    // AP-NL7: no new plan while inject/plan already in flight.
    if (EbHookOrchestration::injecting() || EbHookOrchestration::planning())
        return;
    bool explore = false;
    if (!armFromRoute(layer, explore))
        return;
    if (m_scheduledPressTick > tick || m_scheduledReleaseTick > tick) {
        m_nextProbeTick = std::max(m_scheduledPressTick, m_scheduledReleaseTick) + 1;
        return;
    }
    if (m_scheduledPressTick == tick) {
        m_scheduledPressTick = -1;
        press(layer, explore);
    }
}

bool EbNeuralAutoplay::selfCheck(std::string& report) {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };
    EbNeuralAutoplay bot;
    expect(!bot.enabled(), "off-default");
    bot.setEnabled(true);
    expect(bot.enabled(), "on");
    bot.setTimeWarp(16.f);
    expect(bot.timeWarp() == 16.f, "warp");
    bot.setExploreRate(0.2f);
    expect(bot.exploreRate() > 0.19f && bot.exploreRate() < 0.21f, "explore");
    bot.setEnabled(false);
    expect(!bot.enabled(), "off-again");
    expect(bot.queuedRoutes() == 0, "queue-empty");

    std::string metricsReport;
    expect(EbAutoplayMetrics::selfCheck(metricsReport), "autoplay-metrics");

    // Fidelity binder smoke via metrics path.
    bot.m_metrics.noteFidelity(true);
    expect(bot.metrics().fidelityOk() == 1, "metrics-access");

    // CPS window: under cap allows, over cap denies.
    bot.m_maxAutoplayCps = 2;
    bot.m_recentPressTicks = {100, 110};
    expect(!bot.cpsAllowsPress(150), "cps-block");
    bot.m_recentPressTicks = {100};
    expect(bot.cpsAllowsPress(150), "cps-allow");

    if (fails == 0) {
        report = "NeuralAutoplay OK";
        return true;
    }
    report = std::string("NeuralAutoplay fails: ") + report;
    return false;
}
