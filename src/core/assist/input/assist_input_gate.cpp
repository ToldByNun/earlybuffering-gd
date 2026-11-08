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

bool AssistInputGate::continuousModeKind(GameModeKind kind) {
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

bool AssistInputGate::continuousModePlayer(PlayerObject* player) {
    if (!(!player)) {
        return AssistController::continuousModeKind(StageConfig::kindOf(player));
    }
    return false;
}

bool AssistInputGate::releaseAssistModeKind(GameModeKind kind) {
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

bool AssistInputGate::releaseAssistModePlayer(PlayerObject* player) {
    if (!(!player)) {
        return AssistController::releaseAssistModeKind(StageConfig::kindOf(player));
    }
    return false;
}

bool AssistInputGate::shouldPlanRelease(AssistController const& controller,
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

    auto& bake = BakeMap::get();
    if (!bake.loaded() || bake.data().clicks.empty())
        return false;

    double tps = bake.data().tps > 0.0 ? bake.data().tps : AssistClock::kNominalTps;
    float const simDt = controller.m_simDt > 0.f
                            ? controller.m_simDt
                            : static_cast<float>(1.0 / AssistClock::kNominalTps);
    int const approxFrame = AssistClock::get().remapTick(
        tps, controller.m_attemptTick > 0 ? controller.m_attemptTick : effectTick, simDt);
    auto const kind = StageConfig::kindOf(player);
    auto const* win = bake.findNear(player->getPositionX(), approxFrame, false, !player1, kind);
    return win && win->holdAssist;
}

bool AssistInputGate::releaseGateSelfCheck(std::string& report) {
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

    expect(AssistController::releaseAssistModeKind(GameModeKind::Wave), "rel-wave");
    expect(AssistController::releaseAssistModeKind(GameModeKind::Ship), "rel-ship");
    expect(AssistController::releaseAssistModeKind(GameModeKind::Swing), "rel-swing");
    expect(AssistController::releaseAssistModeKind(GameModeKind::Robot), "rel-robot");
    expect(!AssistController::releaseAssistModeKind(GameModeKind::Cube), "rel-not-cube");
    expect(!AssistController::releaseAssistModeKind(GameModeKind::Ball), "rel-not-ball");
    expect(!AssistController::releaseAssistModeKind(GameModeKind::Ufo), "rel-not-ufo");
    expect(!AssistController::releaseAssistModeKind(GameModeKind::Spider), "rel-not-spider");
    // Viz parity: BakePathVisualizer must match this set.
    expect(BakePathVisualizer::releaseWindowMode(GameModeKind::Wave) ==
               AssistController::releaseAssistModeKind(GameModeKind::Wave),
           "viz-wave");
    expect(BakePathVisualizer::releaseWindowMode(GameModeKind::Cube) ==
               AssistController::releaseAssistModeKind(GameModeKind::Cube),
           "viz-cube");
    expect(BakePathVisualizer::releaseWindowMode(GameModeKind::Robot) ==
               AssistController::releaseAssistModeKind(GameModeKind::Robot),
           "viz-robot");

    if (fails == 0) {
        report = "releaseGateSelfCheck OK";
        return true;
    }
    report = fmt::format("releaseGateSelfCheck {} fails: {}", fails, report);
    return false;
}

int AssistInputGate::activeLimit(AssistController const& controller, PlayerObject* player) {
    return StageConfig::paramsFor(player).ticks;
}

bool AssistInputGate::shouldAssistPlayer(AssistController const& controller, PlayerObject* player) {
    if (!StageConfig::isEnabled())
        return false;
    if (!player || player->m_isDead)
        return false;
    if (controller.cpsLimited())
        return false;
    if (!StageConfig::paramsFor(player).enabled)
        return false;
    if (!(!AssistScopeGate::allowAssist(PlayLayer::get(), player))) {
        return true;
    }
    return false;
}

void AssistInputGate::pruneClickTimes(AssistController& controller) {
    auto const now = std::chrono::steady_clock::now();
    while (!controller.m_clickTimes.empty() &&
           now - controller.m_clickTimes.front() > std::chrono::seconds(1)) {
        controller.m_clickTimes.pop_front();
    }
}

void AssistInputGate::noteUserClick(AssistController& controller) {
    controller.m_clickTimes.push_back(std::chrono::steady_clock::now());
    controller.pruneClickTimes();
    int const limitValue = StageConfig::cpsLimit();
    if (limitValue > 0 && static_cast<int>(controller.m_clickTimes.size()) >= limitValue) {
        if (!controller.m_cpsLocked) {
            controller.m_cpsLocked = true;
            AssistLog::info(fmt::format("CPS: limit hit ({} >= {}) — assist paused",
                                        controller.m_clickTimes.size(), limitValue));
        }
    }
}

int AssistInputGate::currentCps(AssistController const& controller) {
    auto const now = std::chrono::steady_clock::now();
    int count = 0;
    for (auto const& t : controller.m_clickTimes) {
        if (now - t <= std::chrono::seconds(1))
            ++count;
    }
    return count;
}

bool AssistInputGate::cpsLimited(AssistController const& controller) {
    int const limit = StageConfig::cpsLimit();
    if (!(limit <= 0)) {
        return controller.currentCps() >= limit;
    }
    return false;
}
