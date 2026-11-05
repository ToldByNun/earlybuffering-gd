#ifndef EARLYBUFFER_CORE_SHIFTEDINPUTQUEUE_HPP
#define EARLYBUFFER_CORE_SHIFTEDINPUTQUEUE_HPP

#include <string>

// FIFO of follow-up presses/releases that share one Delay shift (CBF-style).
// Cap 8. No auto-click/release — only stores real user inputs for later inject.
struct ShiftedInput {
    bool down = true;
    bool player1 = true;
    int pressTick = 0; // original effect tick
    int fireTick = 0;  // pressTick + shared shift
};

class ShiftedInputQueue {
public:
    static constexpr int kCap = 8;

    void clear();
    // Begin a new shared shift (clears prior entries). shiftTicks >= 0.
    void beginShift(int shiftTicks);
    int shift() const { return m_shift; }
    bool active() const { return m_active; }
    bool empty() const { return m_count <= 0; }
    int size() const { return m_count; }

    // Enqueue with current shift. false = overflow (caller may PassQueueOverflow).
    bool enqueue(bool down, bool player1, int pressTick);

    bool tryPeek(ShiftedInput& out) const;
    // Pop front if fireTick <= attemptTick.
    bool tryPopDue(int attemptTick, ShiftedInput& out);
    // Spam-Pass drain: pop front regardless of due (ordered 1/call).
    bool tryDrainOne(ShiftedInput& out);

    static bool selfCheck(std::string& report);

private:
    bool m_active = false;
    int m_shift = 0;
    ShiftedInput m_items[kCap]{};
    int m_head = 0;
    int m_count = 0;
};

#endif // EARLYBUFFER_CORE_SHIFTEDINPUTQUEUE_HPP
