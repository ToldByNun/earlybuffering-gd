#include "assist_input_gate.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../../ui/visualizers/bake_path_visualizer.hpp"
#include "../../bake/bake_map.hpp"
#include "../assist_clock.hpp"
#include "../assist_controller.hpp"
#include "../assist_scope_gate.hpp"
#include <chrono>
#include <string>

bool EbAssistInputGate::continuousModeKind(GameModeKind kind) {
    switch (kind) {
    case GameModeKind::Wave:
    case GameModeKind::Ship:
    case GameModeKind::Swing:
    case GameModeKind::Ufo:
    case GameModeKind::Spider:
        return true;
    default:
        return false;
    }
}

bool EbAssistInputGate::continuousModePlayer(PlayerObject* player) {
    if (!(!player)) {
        return EbAssistController::continuousModeKind(EbStageConfig::kindOf(player));
    }
    return false;
}

bool EbAssistInputGate::releaseAssistModeKind(GameModeKind kind) {
    // Same set as BakePathVisualizer purple windows (not ufo/spider).
    switch (kind) {
    case GameModeKind::Wave:
    case GameModeKind::Ship:
    case GameModeKind::Swing:
    case GameModeKind::Robot:
        return true;
    default:
        return false;
    }
}

bool EbAssistInputGate::releaseAssistModePlayer(PlayerObject* player) {
    if (!(!player)) {
        return EbAssistController::releaseAssistModeKind(EbStageConfig::kindOf(player));
    }
    return false;
}

bool EbAssistInputGate::shouldPlanRelease(EbAssistController const& controller,
                                        PlayLayer* /*playLayer*/, PlayerObject* player,
                                        bool player1, int effectTick) {
    if (!player)
        return false;
    if (controller.releaseAssistModePlayer(player))
        return true;

    // Discrete (cube/ball/ufo/spider): only dash / hold-orb contexts.
    if (player->m_isDashing)
        return true;
    if (controller.m_pendingReleaseHold)
        return true;
    if (controller.m_buffer.requireHold)
        return true;

    auto& bake = EbBakeMap::get();
    if (!bake.loaded() || bake.data().clicks.empty())
        return false;

    double tps = bake.data().tps > 0.0 ? bake.data().tps : EbAssistClock::kNominalTps;
    float const simDt = controller.m_simDt > 0.f
                            ? controller.m_simDt
                            : static_cast<float>(1.0 / EbAssistClock::kNominalTps);
    int const approxFrame = EbAssistClock::get().remapTick(
        tps, controller.m_attemptTick > 0 ? controller.m_attemptTick : effectTick, simDt);
    auto const kind = EbStageConfig::kindOf(player);
    auto const* win = bake.findNear(player->getPositionX(), approxFrame, false, !player1, kind);
    return win && win->holdAssist;
}

bool EbAssistInputGate::releaseGateSelfCheck(std::string& report) {
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

    expect(EbAssistController::releaseAssistModeKind(GameModeKind::Wave), "rel-wave");
    expect(EbAssistController::releaseAssistModeKind(GameModeKind::Ship), "rel-ship");
    expect(EbAssistController::releaseAssistModeKind(GameModeKind::Swing), "rel-swing");
    expect(EbAssistController::releaseAssistModeKind(GameModeKind::Robot), "rel-robot");
    expect(!EbAssistController::releaseAssistModeKind(GameModeKind::Cube), "rel-not-cube");
    expect(!EbAssistController::releaseAssistModeKind(GameModeKind::Ball), "rel-not-ball");
    expect(!EbAssistController::releaseAssistModeKind(GameModeKind::Ufo), "rel-not-ufo");
    expect(!EbAssistController::releaseAssistModeKind(GameModeKind::Spider), "rel-not-spider");
    // Viz parity: BakePathVisualizer must match this set.
    expect(EbBakePathVisualizer::releaseWindowMode(GameModeKind::Wave) ==
               EbAssistController::releaseAssistModeKind(GameModeKind::Wave),
           "viz-wave");
    expect(EbBakePathVisualizer::releaseWindowMode(GameModeKind::Cube) ==
               EbAssistController::releaseAssistModeKind(GameModeKind::Cube),
           "viz-cube");
    expect(EbBakePathVisualizer::releaseWindowMode(GameModeKind::Robot) ==
               EbAssistController::releaseAssistModeKind(GameModeKind::Robot),
           "viz-robot");

    if (fails == 0) {
        report = "releaseGateSelfCheck OK";
        return true;
    }
    report = fmt::format("releaseGateSelfCheck {} fails: {}", fails, report);
    return false;
}

int EbAssistInputGate::activeLimit(EbAssistController const& controller, PlayerObject* player) {
    return EbStageConfig::paramsFor(player).ticks;
}

bool EbAssistInputGate::shouldAssistPlayer(EbAssistController const& controller, PlayerObject* player) {
    if (!EbStageConfig::isEnabled())
        return false;
    if (!player || player->m_isDead)
        return false;
    if (controller.cpsLimited())
        return false;
    if (!EbStageConfig::paramsFor(player).enabled)
        return false;
    if (!(!EbAssistScopeGate::allowAssist(PlayLayer::get(), player))) {
        return true;
    }
    return false;
}

void EbAssistInputGate::pruneClickTimes(EbAssistController& controller) {
    auto const now = std::chrono::steady_clock::now();
    while (!controller.m_clickTimes.empty() &&
           now - controller.m_clickTimes.front() > std::chrono::seconds(1)) {
        controller.m_clickTimes.pop_front();
    }
}

void EbAssistInputGate::noteUserClick(EbAssistController& controller) {
    controller.m_clickTimes.push_back(std::chrono::steady_clock::now());
    controller.pruneClickTimes();
    int const limitValue = EbStageConfig::cpsLimit();
    if (limitValue > 0 && static_cast<int>(controller.m_clickTimes.size()) >= limitValue) {
        if (!controller.m_cpsLocked) {
            controller.m_cpsLocked = true;
            EbAssistLog::info(fmt::format("CPS: limit hit ({} >= {}) — assist paused",
                                        controller.m_clickTimes.size(), limitValue));
        }
    }
}

int EbAssistInputGate::currentCps(EbAssistController const& controller) {
    auto const now = std::chrono::steady_clock::now();
    int count = 0;
    for (auto const& t : controller.m_clickTimes) {
        if (now - t <= std::chrono::seconds(1))
            ++count;
    }
    return count;
}

bool EbAssistInputGate::cpsLimited(EbAssistController const& controller) {
    int const limit = EbStageConfig::cpsLimit();
    if (!(limit <= 0)) {
        return controller.currentCps() >= limit;
    }
    return false;
}
