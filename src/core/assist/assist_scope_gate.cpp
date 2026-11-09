#include "assist_scope_gate.hpp"
#include "../../config/stage_config.hpp"
#include <Geode/Geode.hpp>

bool EbAssistScopeGate::isPlatformer(PlayerObject* player) {
    return player && player->m_isPlatformer;
}

bool EbAssistScopeGate::allowAssist(PlayLayer* layer, PlayerObject* player) {
    return blockReason(layer, player) == nullptr;
}

char const* EbAssistScopeGate::blockReason(PlayLayer* layer, PlayerObject* player) {
    if (!player)
        return "no-player";
    if (isPlatformer(player))
        return "platformer";
    if (EbStageConfig::assistOnlyPractice()) {
        if (!layer || !layer->m_isPracticeMode)
            return "practice-only";
    }
    if (layer) {
        float const pct = layer->getCurrentPercent();
        int const lowerBound = EbStageConfig::assistPercentMin();
        int const upperBound = EbStageConfig::assistPercentMax();
        if ((lowerBound > 0 || upperBound < 100) &&
            (pct + 0.01f < (float)lowerBound || pct - 0.01f > (float)upperBound)) {
            return "percent-gate";
        }
        if (EbStageConfig::isInAssistDisableRange(pct)) {
            return "percent-disable";
        }
    }
    if (EbStageConfig::assistStartposOnly() && layer && !layer->m_isTestMode) {
        return "startpos-only";
    }
    return nullptr;
}
