#ifndef FRIRECRACKER_CORE_BUFFEREVENTMACHINE_HPP
#define FRIRECRACKER_CORE_BUFFEREVENTMACHINE_HPP

enum class BufferSmState {
    Idle = 0,
    ArmedPress,
    ArmedRelease,
    PendingRelease
};

// Documents + validates buffer transition rules (runtime still uses BufferedInput).
class BufferEventMachine {
public:
    static bool selfCheck();
    static char const* stateName(BufferSmState s);
};

#endif