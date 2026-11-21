#include "neural_autoplay.hpp"
#include "../../app/class_manager.hpp"
#include "../../config/stage_config.hpp"
#include "../../debug/assist_log.hpp"
#include "../assist/assist_controller.hpp"
#include "../sim/shadow_simulator.hpp"
#include "ai_config.hpp"
#include "ai_route_planner.hpp"
#include "neural_learning.hpp"
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
    clearSchedule();
    clearRoute();
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

void EbNeuralAutoplay::press(PlayLayer* layer, bool explore) {
    if (!layer)
        return;
    int const jump = EbAssistControllerImplementation::JUMP_BUTTON;
    layer->handleButton(true, jump, true);
    m_holding = true;
    m_holdUntilTick = -1; // release comes from scheduled route event when present
    m_plannedHoldTicks = -1;
    ++m_presses;
    if (explore)
        ++m_explorePresses;
    // If the route has an explicit Release next, schedule it; else fall back to hold ticks.
    if (!scheduleNextRouteEvent(layer) && m_holding) {
        int const hold = holdTicksForPlayer(layer);
        m_holdUntilTick = EbAssistController::get().attemptTick() + hold;
    }
}

void EbNeuralAutoplay::release(PlayLayer* layer) {
    if (!layer)
        return;
    auto& controller = EbAssistController::get();
    int const jump = EbAssistControllerImplementation::JUMP_BUTTON;
    layer->handleButton(false, jump, true);
    m_holding = false;
    m_holdUntilTick = -1;
    m_scheduledReleaseTick = -1;
    m_nextPressTick = controller.attemptTick() + m_gapTicks;
    m_nextProbeTick = m_nextPressTick;
    if (!scheduleNextRouteEvent(layer))
        clearRoute();
}

bool EbNeuralAutoplay::scheduleNextRouteEvent(PlayLayer* layer) {
    if (!layer || m_activeRoute.events.empty())
        return false;
    if (m_routeIndex >= static_cast<int>(m_activeRoute.events.size())) {
        clearRoute();
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
        return false;
    }
    ++m_probes;

    int const limit = std::max(1, controller.activeLimit(player));
    float const dt = controller.simDt() > 0.f ? controller.simDt() : (1.f / 240.f);
    int const tick = controller.attemptTick();
    float const liveX = player->getPositionX();

    auto& planner = EbAiRoutePlanner::get();
    if (cfg.replanOnDivergence && planner.needsReplan(tick, liveX))
        planner.invalidate("divergence");

    EbAiRoute route = planner.plan(layer, player, true, tick, limit, dt);
    if (!route.valid || route.events.empty()) {
        if (m_exploreRate > 0.f) {
            float const roll = static_cast<float>(std::rand() % 10000) / 10000.f;
            if (roll < m_exploreRate) {
                exploreOut = true;
                m_plannedHoldTicks = holdTicksForPlayer(layer);
                m_lastProbeReason = "explore";
                m_activeRoute.clear();
                m_routeIndex = 0;
                m_scheduledPressTick = tick;
                m_scheduledReleaseTick = -1;
                return true;
            }
        }
        m_lastProbeReason = route.reason ? route.reason : "no-route";
        return false;
    }

    m_activeRoute = route;
    m_routeIndex = 0;
    if (!scheduleNextRouteEvent(layer)) {
        clearRoute();
        m_lastProbeReason = "no-event";
        return false;
    }
    return true;
}

void EbNeuralAutoplay::onDeath(PlayLayer* layer) {
    if (!m_enabled)
        return;
    m_holding = false;
    m_holdUntilTick = -1;
    clearSchedule();
    clearRoute();
    EbAiRoutePlanner::get().invalidate("death");
    if (!m_restartOnDeath || !layer)
        return;
    m_pendingRestart = true;
    m_restartAtTick = EbAssistController::get().attemptTick() + 30;
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
    bool explore = false;
    if (!armFromRoute(layer, explore))
        return;
    if (m_scheduledPressTick > tick || m_scheduledReleaseTick > tick) {
        m_nextProbeTick =
            std::max(m_scheduledPressTick, m_scheduledReleaseTick) + 1;
        return;
    }
    if (m_scheduledPressTick == tick) {
        m_scheduledPressTick = -1;
        press(layer, explore);
    }
}
