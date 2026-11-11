# Bake & Trigger World

## Bake maps (Replay / Hybrid / BruteForce)

A **bake** is a list of timing windows for places in a level. It is not a bot that plays for you.

| Action idea | Meaning |
|---|---|
| Import / replay bake | Build windows from a macro / replay drop |
| Brute bake | Search windows without a replay |
| Hybrid | Use a nearby window only after live verify says it clears threat |

If verify runs out of time (`budgetHit`), EarlyBuffer must **not** soft-trust that bake window.

## Trigger World Bake

Movers / toggles can be invisible to a dumb shadow. Trigger World Bake caches spawn-timed static poses so the sim can collide them better.

Useful toggles:

- Trigger World Bake on/off
- Optional skip of some player-follow dynamics (faster, less accurate)

If movers still feel “wrong” in assist, this is often the first place to check — plus honesty / unknown paths in newer builds.

Details: [Assist reference](/assist_reference).
