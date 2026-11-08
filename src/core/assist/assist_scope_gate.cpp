#include "assist_scope_gate.hpp"
#include "../../config/stage_config.hpp"
#include <Geode/Geode.hpp>

bool AssistScopeGate::isPlatformer(PlayerObject* player) {
    return player && player->m_isPlatformer;
}

bool AssistScopeGate::allowAssist(PlayLayer* layer, PlayerObject* player) {
    return blockReason(layer, player) == nullptr;
}

char const* AssistScopeGate::blockReason(PlayLayer* layer, PlayerObject* player) {
    if (!player)
        return "no-player";
    if (isPlatformer(player))
        return "platformer";
    if (StageConfig::assistOnlyPractice()) {
        if (!layer || !layer->m_isPracticeMode)
            return "practice-only";
    }
    if (layer) {
        float const pct = layer->getCurrentPercent();
        int const lowerBound = StageConfig::assistPercentMin();
        int const upperBound = StageConfig::assistPercentMax();
        if ((lowerBound > 0 || upperBound < 100) &&
            (pct + 0.01f < (float)lowerBound || pct - 0.01f > (float)upperBound)) {
            return "percent-gate";
        }
        if (StageConfig::isInAssistDisableRange(pct)) {
            return "percent-disable";
        }
    }
    if (StageConfig::assistStartposOnly() && layer && !layer->m_isTestMode) {
        return "startpos-only";
    }
    return nullptr;
}
