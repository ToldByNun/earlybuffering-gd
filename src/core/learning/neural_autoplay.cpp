#include "neural_autoplay.hpp"
#include "../../app/class_manager.hpp"
#include "../../config/stage_config.hpp"
#include "../../debug/assist_log.hpp"
#include "../assist/assist_controller.hpp"
#include "../sim/planning/simulation_plan.hpp"
#include "../sim/shadow_simulator.hpp"
#include "neural_learning.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cstdlib>

#if defined(EB_DESKTOP_IMGUI)
#include "../../ui/desktop_menu/desktop_menu.hpp"
#endif

EbNeuralAutoplay& EbNeuralAutoplay::get() {
    return EbClassManager::get().service<EbNeuralAutoplay>();
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
    auto& controller = EbAssistController::get();
    int const jump = EbAssistControllerImplementation::JUMP_BUTTON;
    // User-path edge (not an inject) so AI planning / session capture run normally.
    layer->handleButton(true, jump, true);
    m_holding = true;
    m_holdUntilTick = controller.attemptTick() + holdTicksForPlayer(layer);
    ++m_presses;
    if (explore)
        ++m_explorePresses;
}

void EbNeuralAutoplay::release(PlayLayer* layer) {
    if (!layer)
        return;
    auto& controller = EbAssistController::get();
    int const jump = EbAssistControllerImplementation::JUMP_BUTTON;
    layer->handleButton(false, jump, true);
    m_holding = false;
    m_holdUntilTick = -1;
    m_nextPressTick = controller.attemptTick() + m_gapTicks;
    m_nextProbeTick = m_nextPressTick;
}

bool EbNeuralAutoplay::shouldPress(PlayLayer* layer, bool& exploreOut) {
    exploreOut = false;
    m_lastProbeReason = "skip";
    if (!layer || !layer->m_player1)
        return false;
    auto* player = layer->m_player1;
    auto& controller = EbAssistController::get();
    auto& neural = EbNeuralLearning::get();
    if (!EbNeuralLearning::active() || !neural.loaded()) {
        m_lastProbeReason = "not-loaded";
        return false;
    }
    ++m_probes;

    int const limit = std::max(1, controller.activeLimit(player));
    float const dt = controller.simDt() > 0.f ? controller.simDt() : (1.f / 240.f);
    int const tick = controller.attemptTick();
    EbPlanOptions prepOpts{};
    (void)neural.prepare(layer, player, true, true, tick, limit, dt, dt, prepOpts);
    auto order = neural.rankedVerifyDelays(limit);

    EbPlanOptions vopts{};
    vopts.budgetUs = 1500;
    vopts.tickBudget = 400;
    vopts.requireAliveForDelay = true;
    vopts.attemptStartTick = tick;
    auto& sim = EbShadowSimulator::get();

    bool useful = false;
    int chosenDelay = 0;
    // Walk top-k — do not stop at d=0 collapse; look for a useful delay>0.
    for (int delay : order) {
        if (delay <= 0)
            continue;
        delay = std::min(delay, limit);
        auto const vr = sim.verifyDelay(layer, player, true, delay, limit, dt, dt, vopts);
        if (vr.budgetHit || vr.unknown)
            continue;
        if (vr.ok) {
            useful = true;
            chosenDelay = delay;
            m_lastProbeReason = "verify-safe";
            break;
        }
        if (vr.clearsThreat && !vr.diedBeforeAction) {
            useful = true;
            chosenDelay = delay;
            m_lastProbeReason = "clears-threat";
            break;
        }
        auto const d0 = sim.verifyDelay(layer, player, true, 0, limit, dt, dt, vopts);
        if (!d0.budgetHit && !d0.unknown && !d0.ok && (vr.ok || vr.clearsThreat)) {
            useful = true;
            chosenDelay = delay;
            m_lastProbeReason = "ascent";
            break;
        }
    }

    if (!useful) {
        bool const nearOrb =
            EbAssistControllerImplementation::nearOrbOrPadHint(layer, player, limit, dt);
        int const delayHint = order.empty() ? 0 : std::max(0, order.front());
        float const conf = neural.confidenceForDelay(delayHint > 0 ? delayHint : 0);
        if (nearOrb && conf >= 0.35f) {
            useful = true;
            chosenDelay = delayHint;
            m_lastProbeReason = "orb-hint";
        }
    }
    (void)chosenDelay;

    if (!useful && m_exploreRate > 0.f) {
        float const roll = static_cast<float>(std::rand() % 10000) / 10000.f;
        if (roll < m_exploreRate) {
            useful = true;
            exploreOut = true;
            m_lastProbeReason = "explore";
        }
    }

    if (!useful)
        m_lastProbeReason = "wait";
    return useful;
}

void EbNeuralAutoplay::onDeath(PlayLayer* layer) {
    if (!m_enabled)
        return;
    m_holding = false;
    m_holdUntilTick = -1;
    // Session auto-train runs from EbNeuralLearning::death (IL + Autoplay).
    if (!m_restartOnDeath || !layer)
        return;
    m_pendingRestart = true;
    m_restartAtTick = EbAssistController::get().attemptTick() + 30;
}

void EbNeuralAutoplay::onPhysicsTick(PlayLayer* layer) {
    if (!layer)
        return;

    // Warp sync even when other gates fail so disable restores mid-attempt.
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

    if (m_holding) {
        if (m_holdUntilTick >= 0 && tick >= m_holdUntilTick)
            release(layer);
        return;
    }

    if (tick < m_nextPressTick)
        return;
    if (controller.buffer().state != BufferState::Idle)
        return;
    // Do not steal a live human hold.
    if (controller.userHolding(true))
        return;
    if (tick < 8)
        return;
    if (tick < m_nextProbeTick)
        return;

    m_nextProbeTick = tick + m_probeEveryTicks;
    bool explore = false;
    if (!shouldPress(layer, explore))
        return;
    press(layer, explore);
}
