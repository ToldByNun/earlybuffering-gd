#include "ShiftedInputQueue.hpp"

#include <algorithm>
#include <fmt/format.h>

void ShiftedInputQueue::clear() {
    m_active = false;
    m_shift = 0;
    m_head = 0;
    m_count = 0;
}

void ShiftedInputQueue::beginShift(int shiftTicks) {
    clear();
    m_active = true;
    m_shift = std::max(0, shiftTicks);
}

bool ShiftedInputQueue::enqueue(bool down, bool player1, int pressTick) {
    if (!m_active) return false;
    if (m_count >= kCap) return false;
    int const idx = (m_head + m_count) % kCap;
    ShiftedInput& slot = m_items[idx];
    slot.down = down;
    slot.player1 = player1;
    slot.pressTick = pressTick;
    slot.fireTick = pressTick + m_shift;
    ++m_count;
    return true;
}

bool ShiftedInputQueue::tryPeek(ShiftedInput& out) const {
    if (m_count <= 0) return false;
    out = m_items[m_head];
    return true;
}

bool ShiftedInputQueue::tryPopDue(int attemptTick, ShiftedInput& out) {
    if (m_count <= 0) return false;
    if (m_items[m_head].fireTick > attemptTick) return false;
    out = m_items[m_head];
    m_head = (m_head + 1) % kCap;
    --m_count;
    if (m_count <= 0) {
        m_head = 0;
    }
    return true;
}

bool ShiftedInputQueue::tryDrainOne(ShiftedInput& out) {
    if (m_count <= 0) return false;
    out = m_items[m_head];
    m_head = (m_head + 1) % kCap;
    --m_count;
    if (m_count <= 0) {
        m_head = 0;
    }
    return true;
}

bool ShiftedInputQueue::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    ShiftedInputQueue q;
    expect(q.empty() && !q.active() && q.shift() == 0, "fresh");

    q.beginShift(4);
    expect(q.active() && q.shift() == 4 && q.empty(), "begin");
    expect(q.enqueue(true, true, 100), "enq1");
    expect(q.enqueue(false, true, 103), "enq2"); // hold length 3 → release at 107
    expect(q.size() == 2, "size2");

    ShiftedInput peek{};
    expect(q.tryPeek(peek) && peek.down && peek.fireTick == 104, "peek-down");

    ShiftedInput due{};
    expect(!q.tryPopDue(103, due), "not-due");
    expect(q.tryPopDue(104, due) && due.down && due.fireTick == 104, "pop-due");
    expect(q.size() == 1, "size1");
    expect(q.tryPopDue(107, due) && !due.down && due.fireTick == 107, "pop-rel");
    expect(q.empty(), "empty-after");

    // Cap 8 overflow
    q.beginShift(2);
    for (int i = 0; i < ShiftedInputQueue::kCap; ++i) {
        expect(q.enqueue(true, true, 200 + i), "fill");
    }
    expect(!q.enqueue(true, true, 300), "overflow");
    expect(q.size() == ShiftedInputQueue::kCap, "full");

    // Drain one ignores due
    ShiftedInput drain{};
    expect(q.tryDrainOne(drain) && drain.pressTick == 200, "drain0");
    expect(q.size() == ShiftedInputQueue::kCap - 1, "drain-size");

    // beginShift clears
    q.beginShift(1);
    expect(q.empty() && q.shift() == 1, "rebegin");

    // Negative shift clamps
    q.beginShift(-3);
    expect(q.shift() == 0, "neg-clamp");
    expect(q.enqueue(true, false, 50), "p2");
    expect(q.tryPopDue(50, due) && !due.player1 && due.fireTick == 50, "p2-due");

    if (fails == 0) {
        report = "ShiftedInputQueue selfCheck OK";
        return true;
    }
    report = fmt::format("ShiftedInputQueue {} fails: {}", fails, report);
    return false;
}
