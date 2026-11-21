#include "neural_autoplay.hpp"
#include "../../app/class_manager.hpp"
#include "../../config/stage_config.hpp"
#include "../../debug/assist_log.hpp"
#include "../assist/assist_controller.hpp"
#include "../sim/shadow_simulator.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>

#if defined(EB_DESKTOP_IMGUI)
#include "../../ui/desktop_menu/desktop_menu.hpp"
#endif

EbNeuralAutoplay& EbNeuralAutoplay::get() {
    return EbClassManager::get().service<EbNeuralAutoplay>();
}

void EbNeuralAutoplay::onAttemptReset() {
    m_holding = false;
    m_holdUntilTick = -1;
    m_pendingRestart = false;
    m_restartAtTick = -1;
    m_nextPressTick = 0;
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

void EbNeuralAutoplay::press(PlayLayer* layer) {
    if (!layer)
        return;
    auto& controller = EbAssistController::get();
    int const jump = EbAssistControllerImplementation::JUMP_BUTTON;
    // User-path edge (not an inject) so AI planning / session capture run normally.
    layer->handleButton(true, jump, true);
    m_holding = true;
    m_holdUntilTick = controller.attemptTick() + holdTicksForPlayer(layer);
    ++m_presses;
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
}

void EbNeuralAutoplay::onDeath(PlayLayer* layer) {
    if (!m_enabled)
        return;
    m_holding = false;
    m_holdUntilTick = -1;
    if (!m_restartOnDeath || !layer)
        return;
    m_pendingRestart = true;
    m_restartAtTick = EbAssistController::get().attemptTick() + 30;
}

void EbNeuralAutoplay::onPhysicsTick(PlayLayer* layer) {
    if (!m_enabled || !layer)
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

    press(layer);
}
