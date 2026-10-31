#ifndef EARLYBUFFER_CORE_BUFFEREVENTMACHINE_HPP
#define EARLYBUFFER_CORE_BUFFEREVENTMACHINE_HPP

enum class BufferSmState {
    Idle = 0,
    ArmedPress,
    ArmedRelease,
    PendingRelease
};

// Documents + validates buffer transition rules (runtime still uses BufferedInput).
//
// Hold / Delay press contract (must stay true in AssistController):
//   Idle → ArmedPress on Delay planPress (swallow DOWN).
//   Early UP while ArmedPress: set releaseSeen, stay ArmedPress — fire still due.
//   At fireTick: inject; releaseSeen → PendingRelease (ASAP today; requireHold
//   honor is a separate gameplay change).
//   Press B while ArmedPress + releaseSeen: fire A, then Idle, then maybe re-arm B.
//   Cancel (death/reset/quit/checkpoint) → Idle. Force-fire (CPS/mode) still injects.
class BufferEventMachine {
public:
    static bool selfCheck();
    static char const* stateName(BufferSmState s);
};

#endif
