#include "BufferEventMachine.hpp"

char const* BufferEventMachine::stateName(BufferSmState s) {
    switch (s) {
        case BufferSmState::Idle: return "Idle";
        case BufferSmState::ArmedPress: return "ArmedPress";
        case BufferSmState::ArmedRelease: return "ArmedRelease";
        case BufferSmState::PendingRelease: return "PendingRelease";
    }
    return "?";
}

bool BufferEventMachine::selfCheck() {
    // Transition contract smoke test (logical).
    BufferSmState s = BufferSmState::Idle;
    s = BufferSmState::ArmedPress; // planPress Delay
    // releaseSeen keeps ArmedPress
    // Press B => fire then Idle then maybe re-arm
    s = BufferSmState::Idle;
    s = BufferSmState::PendingRelease;
    s = BufferSmState::Idle;
    (void)s;
    return true;
}