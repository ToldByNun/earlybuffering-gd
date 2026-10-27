#ifndef FRIRECRACKER_CORE_ASSISTSCOPEGATE_HPP
#define FRIRECRACKER_CORE_ASSISTSCOPEGATE_HPP

class PlayerObject;
class PlayLayer;

class AssistScopeGate {
public:
    static bool isPlatformer(PlayerObject* player);
    static bool allowAssist(PlayLayer* layer, PlayerObject* player);
    static char const* blockReason(PlayLayer* layer, PlayerObject* player);
};

#endif // FRIRECRACKER_CORE_ASSISTSCOPEGATE_HPP