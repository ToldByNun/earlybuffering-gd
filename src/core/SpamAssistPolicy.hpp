#ifndef EARLYBUFFER_CORE_SPAMASSISTPOLICY_HPP
#define EARLYBUFFER_CORE_SPAMASSISTPOLICY_HPP

#include "AssistVerdict.hpp"
#include "ModeAssist.hpp"
#include "ShiftedInputQueue.hpp"

#include <string>

// Pass vs Queue vs AllowAssist for spam / mid-buffer follow-ups.
// Uses ModeAssist gaps + featheringIsControl. No auto-click/release.
enum class SpamAssistAction {
    AllowAssist = 0,   // continue plan / classic path
    PassSpam,          // vanilla Pass (no opportunity)
    EnqueueShifted,    // buffer active → same-shift queue
    DrainSpamPass,     // PassSpam while queue non-empty → drain 1/tick
    OverflowPass,      // queue at Cap → named Pass
};

struct SpamAssistContext {
    GameModeKind kind = GameModeKind::Cube;
    bool densityPass = false;      // ClickIntentGuard would Pass
    bool nearOrbOrPad = false;
    bool nearBake = false;
    bool mustDelaySearch = false;  // PerfectAssist trust+opportunity
    bool bufferActive = false;
    bool releaseSeen = false;      // classic re-click → fire A then plan B
    bool shiftQueueActive = false;
    int queueSize = 0;
    int queueCap = ShiftedInputQueue::kCap;
};

class SpamAssistPolicy {
public:
    static SpamAssistAction decide(SpamAssistContext const& ctx);

    // Map action → AssistReason for Pass paths (None if AllowAssist/Enqueue).
    static AssistReason passReason(SpamAssistAction action);

    // Ship/Swing feathering: density alone must not PassSpam.
    static bool featherBlocksSpamPass(GameModeKind kind);

    // Debounce (min-click-interval) must not block shift-enqueue (plan Entsch. 8).
    static bool debounceBlocksAssist(bool queueOrBufferShiftActive);

    static bool selfCheck(std::string& report);

private:
    SpamAssistPolicy() = delete;
};

#endif // EARLYBUFFER_CORE_SPAMASSISTPOLICY_HPP
