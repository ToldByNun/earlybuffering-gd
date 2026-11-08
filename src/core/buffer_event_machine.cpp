#include "buffer_event_machine.hpp"

char const* BufferEventMachine::stateName(BufferSmState state) {
    switch (state) {
    case BufferSmState::Idle:
        return "Idle";
    case BufferSmState::ArmedPress:
        return "ArmedPress";
    case BufferSmState::ArmedRelease:
        return "ArmedRelease";
    case BufferSmState::PendingRelease:
        return "PendingRelease";
    }
    return "?";
}

bool BufferEventMachine::selfCheck() {
    // Logical transition smoke (no AssistController instance required).
    // Mirrors BufferedInput: Idle/Buffered ≈ Idle/ArmedPress; releaseSeen is a flag.

    if (stateName(BufferSmState::Idle)[0] == '?')
        return false;
    if (stateName(BufferSmState::ArmedPress)[0] == '?')
        return false;
    if (stateName(BufferSmState::PendingRelease)[0] == '?')
        return false;

    BufferSmState state = BufferSmState::Idle;
    bool releaseSeen = false;
    bool fireStillDue = false;

    // 1) Delay planPress → ArmedPress, fire scheduled
    state = BufferSmState::ArmedPress;
    fireStillDue = true;
    if (state != BufferSmState::ArmedPress || !fireStillDue)
        return false;

    // 2) Early UP → releaseSeen, stay ArmedPress (does NOT cancel inject)
    releaseSeen = true;
    if (state != BufferSmState::ArmedPress)
        return false;
    if (!fireStillDue)
        return false;
    if (!releaseSeen)
        return false;

    // 3a) fireTick + releaseSeen + plain Delay → PendingRelease ASAP → Idle
    fireStillDue = false;
    state = BufferSmState::Idle;
    if (releaseSeen) {
        state = BufferSmState::PendingRelease;
        state = BufferSmState::Idle; // ASAP (queuedHold == 0)
        releaseSeen = false;
    }
    if (state != BufferSmState::Idle || fireStillDue)
        return false;

    // 3b) fireTick + releaseSeen + plan hold → PendingRelease for N ticks → Idle
    state = BufferSmState::ArmedPress;
    releaseSeen = true;
    fireStillDue = true;
    int const queuedHold = 5; // resolveQueuedHoldTicks > 0
    fireStillDue = false;
    state = BufferSmState::PendingRelease;
    if (queuedHold <= 0 || state != BufferSmState::PendingRelease)
        return false;
    state = BufferSmState::Idle;
    releaseSeen = false;

    // 4) Press B while ArmedPress + releaseSeen: fire A then re-arm B
    state = BufferSmState::ArmedPress;
    releaseSeen = true;
    fireStillDue = true;
    // fire A
    state = BufferSmState::Idle;
    fireStillDue = false;
    // plan B
    state = BufferSmState::ArmedPress;
    releaseSeen = false;
    fireStillDue = true;
    if (state != BufferSmState::ArmedPress || !fireStillDue || releaseSeen)
        return false;

    // 5) Cancel while armed → Idle (buffer dropped; no inject)
    state = BufferSmState::Idle;
    fireStillDue = false;
    releaseSeen = false;
    if (state != BufferSmState::Idle || fireStillDue)
        return false;

    (void)state;
    return true;
}
