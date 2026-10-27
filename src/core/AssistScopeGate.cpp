#include "AssistScopeGate.hpp"
#include "../config/StageConfig.hpp"
#include <Geode/Geode.hpp>
using namespace geode::prelude;

bool AssistScopeGate::isPlatformer(PlayerObject* player) {
    return player && player->m_isPlatformer;
}

bool AssistScopeGate::allowAssist(PlayLayer* layer, PlayerObject* player) {
    return blockReason(layer, player) == nullptr;
}

char const* AssistScopeGate::blockReason(PlayLayer* layer, PlayerObject* player) {
    if (!player) return "no-player";
    if (isPlatformer(player)) return "platformer";
    if (StageConfig::assistOnlyPractice()) {
        if (!layer || !layer->m_isPracticeMode) return "practice-only";
    }
    if (layer) {
        float const pct = layer->getCurrentPercent();
        int const lo = StageConfig::assistPercentMin();
        int const hi = StageConfig::assistPercentMax();
        if ((lo > 0 || hi < 100) && (pct + 0.01f < (float)lo || pct - 0.01f > (float)hi)) {
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