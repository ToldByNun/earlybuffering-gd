#ifndef ASSIST_SCOPE_GATE_HPP
#define ASSIST_SCOPE_GATE_HPP

class PlayerObject;
class PlayLayer;

class EbAssistScopeGate {
public:
    static bool isPlatformer(PlayerObject* player);
    static bool allowAssist(PlayLayer* layer, PlayerObject* player);
    static char const* blockReason(PlayLayer* layer, PlayerObject* player);

private:
};

#endif
