# Shadow sim

The shadow is a **temporary ghost** of the player + enough world state to ask: “if I jump on this tick, do I die soon?”

## What it is good at

- Static hazards the ghost can collide
- Simple motion
- Optional orb windows
- Cached mover / toggle poses when Trigger World Bake worked

## What can go wrong

| Issue | Plain meaning |
|---|---|
| Unmodeled movers | Ghost does not see that spike slide yet → may look “alive” falsely |
| Budget hit | Ran out of time/ticks → weaker answer, often Pass |
| Mid-step / weird dt | First physics slice odd → harder plans |
| Dual / platformer | Extra rules; platformer is hard-off |

Newer builds add more honesty (unknown / prove past horizon). If you are on an older package, some of those fixes are not in your binary yet.

## Trigger World Bake

Helps the ghost see spawn-timed static movers. See [Bake](/features/bake).
