#ifndef EARLYBUFFER_CORE_ROUTEASSIST_HPP
#define EARLYBUFFER_CORE_ROUTEASSIST_HPP

#include "AssistVerdict.hpp"

class PlayerObject;

// Route-mode admission gate. It never injects input; it merely permits the
// existing short live planner to delay a player-originated input.
class RouteAssist {
public:
    static AssistReason gate(PlayerObject* player, bool player1);
};

#endif
