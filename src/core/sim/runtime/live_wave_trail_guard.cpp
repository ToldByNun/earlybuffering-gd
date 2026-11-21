#include "live_wave_trail_guard.hpp"

HardStreak* EbLiveWaveTrailGuard::s_protectedP1 = nullptr;
HardStreak* EbLiveWaveTrailGuard::s_protectedP2 = nullptr;

void EbLiveWaveTrailGuard::protectPlayer(PlayerObject* player) {
    if (!player)
        return;
    auto* layer = PlayLayer::get();
    if (!layer)
        return;
    if (player == layer->m_player1)
        s_protectedP1 = player->m_waveTrail;
    else if (player == layer->m_player2)
        s_protectedP2 = player->m_waveTrail;
}

void EbLiveWaveTrailGuard::clear() {
    s_protectedP1 = nullptr;
    s_protectedP2 = nullptr;
}

bool EbLiveWaveTrailGuard::isProtected(HardStreak* trail) {
    if (!trail)
        return false;
    if (trail == s_protectedP1 || trail == s_protectedP2)
        return true;
    return isLivePlayerTrail(trail);
}

bool EbLiveWaveTrailGuard::isLivePlayerTrail(HardStreak* trail) {
    if (!trail)
        return false;
    auto* layer = PlayLayer::get();
    if (!layer)
        return false;
    if (layer->m_player1 && layer->m_player1->m_waveTrail == trail)
        return true;
    if (layer->m_player2 && layer->m_player2->m_waveTrail == trail)
        return true;
    return false;
}

bool EbLiveWaveTrailGuard::refuseMutation(HardStreak* trail) {
    return isProtected(trail);
}
