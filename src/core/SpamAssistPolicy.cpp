#include "SpamAssistPolicy.hpp"

#include <fmt/format.h>

bool SpamAssistPolicy::featherBlocksSpamPass(GameModeKind kind) {
    return ModeAssist::forKind(kind).featheringIsControl();
}

bool SpamAssistPolicy::debounceBlocksAssist(bool queueOrBufferShiftActive) {
    // While a shared shift is live, interval must not kill enqueue / assist.
    return !queueOrBufferShiftActive;
}

AssistReason SpamAssistPolicy::passReason(SpamAssistAction action) {
    switch (action) {
    case SpamAssistAction::PassSpam:
    case SpamAssistAction::DrainSpamPass:
        return AssistReason::PassSpam;
    case SpamAssistAction::OverflowPass:
        return AssistReason::PassQueueOverflow;
    default:
        return AssistReason::None;
    }
}

SpamAssistAction SpamAssistPolicy::decide(SpamAssistContext const& ctx) {
    // Mid-buffer follow-up (not classic releaseSeen re-click): same-shift queue.
    if (ctx.bufferActive && ctx.shiftQueueActive && !ctx.releaseSeen) {
        if (ctx.queueSize >= ctx.queueCap) {
            return SpamAssistAction::OverflowPass;
        }
        return SpamAssistAction::EnqueueShifted;
    }

    // Orb / bake / MustDelaySearch → never spam-Pass (AssistCov).
    if (ctx.nearOrbOrPad || ctx.nearBake || ctx.mustDelaySearch) {
        return SpamAssistAction::AllowAssist;
    }

    if (!ctx.densityPass) {
        return SpamAssistAction::AllowAssist;
    }

    // Ship/Swing feathering ≠ PassSpam (ModeAssist featheringIsControl).
    if (featherBlocksSpamPass(ctx.kind)) {
        return SpamAssistAction::AllowAssist;
    }

    // Density Pass without opportunity: drain queued inputs if any.
    if (ctx.queueSize > 0) {
        return SpamAssistAction::DrainSpamPass;
    }
    return SpamAssistAction::PassSpam;
}

bool SpamAssistPolicy::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    expect(featherBlocksSpamPass(GameModeKind::Ship), "ship-feather");
    expect(featherBlocksSpamPass(GameModeKind::Swing), "swing-feather");
    expect(!featherBlocksSpamPass(GameModeKind::Wave), "wave-not-feather");
    expect(!featherBlocksSpamPass(GameModeKind::Cube), "cube-not-feather");

    expect(debounceBlocksAssist(false), "debounce-idle");
    expect(!debounceBlocksAssist(true), "debounce-shift-off");

    // Wave zigzag, no orb → PassSpam
    {
        SpamAssistContext c;
        c.kind = GameModeKind::Wave;
        c.densityPass = true;
        expect(decide(c) == SpamAssistAction::PassSpam, "wave-zigzag-pass");
        expect(passReason(SpamAssistAction::PassSpam) == AssistReason::PassSpam, "reason-spam");
    }

    // Ship density alone → AllowAssist (feather)
    {
        SpamAssistContext c;
        c.kind = GameModeKind::Ship;
        c.densityPass = true;
        expect(decide(c) == SpamAssistAction::AllowAssist, "ship-feather-allow");
    }

    // Orb-train / MustDelaySearch → AllowAssist even on Wave density
    {
        SpamAssistContext c;
        c.kind = GameModeKind::Wave;
        c.densityPass = true;
        c.nearOrbOrPad = true;
        expect(decide(c) == SpamAssistAction::AllowAssist, "orb-train");
        c.nearOrbOrPad = false;
        c.mustDelaySearch = true;
        expect(decide(c) == SpamAssistAction::AllowAssist, "must-delay");
        c.mustDelaySearch = false;
        c.nearBake = true;
        expect(decide(c) == SpamAssistAction::AllowAssist, "near-bake");
    }

    // Mid-buffer enqueue
    {
        SpamAssistContext c;
        c.kind = GameModeKind::Wave;
        c.bufferActive = true;
        c.shiftQueueActive = true;
        c.releaseSeen = false;
        c.queueSize = 2;
        expect(decide(c) == SpamAssistAction::EnqueueShifted, "enq");
        c.queueSize = ShiftedInputQueue::kCap;
        expect(decide(c) == SpamAssistAction::OverflowPass, "overflow");
        expect(
            passReason(SpamAssistAction::OverflowPass) == AssistReason::PassQueueOverflow,
            "reason-overflow"
        );
        // Classic re-click: releaseSeen → not enqueue (AllowAssist / fire A path)
        c.queueSize = 0;
        c.releaseSeen = true;
        c.densityPass = false;
        expect(decide(c) == SpamAssistAction::AllowAssist, "reclick-allow");
    }

    // Drain when PassSpam + queue non-empty
    {
        SpamAssistContext c;
        c.kind = GameModeKind::Wave;
        c.densityPass = true;
        c.queueSize = 3;
        expect(decide(c) == SpamAssistAction::DrainSpamPass, "drain");
    }

    // Rhythm: ModeAssist Wave gaps still drive ClickIntent (parity mirror)
    {
        ModeAssist const& w = ModeAssist::forKind(GameModeKind::Wave);
        ModeAssist const& s = ModeAssist::forKind(GameModeKind::Ship);
        expect(w.spamWindowTicks() == 10 && w.spamMinCount() == 3, "wave-gaps");
        expect(s.spamWindowTicks() == 12 && s.spamMinCount() == 3, "ship-gaps");
        expect(s.spamMaxMeanGap() > w.spamMaxMeanGap(), "ship-gap-gt-wave");
    }

    if (fails == 0) {
        report = "SpamAssistPolicy selfCheck OK";
        return true;
    }
    report = fmt::format("SpamAssistPolicy {} fails: {}", fails, report);
    return false;
}
