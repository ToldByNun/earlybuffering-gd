# Pass vs Delay

Every planned press ends in a verdict.

## Delay

“Fire a few ticks later.”  
EarlyBuffer holds your input and injects the jump on the chosen tick (inside your window).

## Pass

“Fire now / do not buffer.”  
Your click behaves like vanilla for that decision.

Common Pass reasons (names vary in overlay):

| Reason vibe | Meaning |
|---|---|
| PassD0Safe | Jumping now is already fine |
| PassHopeless | Nothing in the window looked better |
| PassNoThreat | No real threat to plan around |
| PassBudget | Planner ran out of time/ticks |
| PassWaveProximity | Wave near hazards — delaying would phase-shift badly |

## Hold rule

While you still hold Jump, EarlyBuffer does **not** force a release. After you let go, it may stretch the hold by at most your window on some modes.

## Overlay

Turn on **In-Level Overlay** or debug logging when you want to see why a click Passed or Delayed.
