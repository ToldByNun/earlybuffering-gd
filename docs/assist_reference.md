# EarlyBuffer Assist Reference

Rebuild / harden guide for Geometry Dash 2.2081 (Geode). Physics unit = tick (measured TPS, often ~240/s). Threat zones use world X.

## Modes (honest)

| Mode | Role |
|------|------|
| Live | Input forgiveness: shadow-plan each click; Delay buffers, Pass = vanilla. |
| Hybrid | Bake window if near, else Live. |
| Replay | Precomputed click windows from imported macro bake (TAS-like timing map). |
| BruteForce | Precomputed exhaustive safe timings (TAS-like generator). Not a live multi-click bot queue. |

Live/Hybrid never block hopeless clicks. Replay/Brute are precomputed windows, not a live macro injector queue.

## Not in scope

- Late-forgiveness (rewind time for late clicks)
- Full move/rotate/scale trigger simulation (Unknown/warn only)
- Multi-click injection queue beyond Smart d0 selection

## Settings table

| Key | Type | Default | Min | Max | Notes |
|-----|------|---------|-----|-----|-------|
| enabled | bool | true | | | Master |
| preset | string | Tight | Tight/Normal/Wide/Max/Off | | Fills defaults |
| stage-index | int | 0 | 0 | 4 | Internal |
| safe-mode | bool | true | | | Block saves when assist enabled |
| show-overlay | bool | true | | | |
| debug-logging | bool | true | | | |
| show-trajectory | bool | false | | | |
| show-pause-bake | bool | true | | | |
| show-pause-assist | bool | true | | | Hotkey mirrors + config I/O |
| assist-only-practice | bool | false | | | |
| assist-startpos-only | bool | false | | | |
| assist-percent-min | int | 0 | 0 | 100 | |
| assist-percent-max | int | 100 | 0 | 100 | |
| save-feedback | string | both | off/icon/sound/both | sound = per-tick MP3; icon = color flash |
| save-icon-color | rgb | #7CFF4A | | Flash color for icon/both |
| assist-off-N-from/to | float | -1 | -1 | 100 | N=1..6; -1 unused; inclusive off range |
| ticks-all | int | 2 | 0 | 32 | Delay window |
| ticks-wave | int | 2 | 0 | 32 | |
| death-slack | int | 2 | 0 | 16 | Look-ahead slack, not zone |
| look-ahead-cap | int | 0 | 0 | 240 | 0=auto |
| swallow-hopeless | bool | false | | | Deprecated unused |
| auto-tune | bool | true | | | Never changes ticks; no trust-up from inject saves |
| assist-mode | string | Hybrid | Live/Replay/Hybrid/BruteForce | | |
| bake-radius | int | 0 | 0 | 128 | 0=auto |
| cps-limit | int | 0 | 0 | 40 | 0=off |
| min-click-interval-ms | int | 0 | 0 | 500 | Skip assist only |
| assist-orbs | bool | true | | | |
| sim-budget-us | int | 1500 | 0 | 10000 | Soft wall-clock; 0=unlimited |
| smart-depth | int | 1 | 1 | 3 | |
| smart-combos | int | 48 | 8 | 256 | |
| smart-horizon | int | 0 | 0 | 320 | 0=auto |
| tick-budget | int | 4000 | 500 | 20000 | Hard tick sum; exhaust=Pass |
| assist-verify | string | off | off/ab | | Debug A/B only |
| assist-press | bool | true | | | |
| assist-release | bool | true | | | |
| MODE-enable/overwrite/ticks/cap/slack | mixed | see mod.json | | | cube..swing |

## Pseudocode plan()

1. tickBudget from opts (default 4000); if verifyAb then *2
2. modelFor; capture state
3. d0 press baseline -> ThreatZone clusterGapX=50
4. eval delays under tick + sim-budget-us
5. orbIntent: earliest +/-k robust activation (NO midpoint)
6. else d0 clearsThreat -> Pass; else earliest +/-k robust viable
7. budgetHit -> Pass; linked dual both clear

Lexico: orbUseful > surviveBin > endX > earliest. Bins 24/48/96. k=1.

## Pseudocode fireBuffered()

Wait for dMin if requireOrb; up to 2 reclamps into [dMin,dMax]; inject; PendingRelease if needed.
Press B while ArmedPress: fire A first, then plan B.

## Buffer states

Idle | ArmedPress | ArmedRelease | PendingRelease
Cancel: death, resetLevel, quit, checkpoint load. Force-fire: mode-change, CPS unlock.

## LevelProfile JSON

key (LevelContentKey fullKey with content rev), modes[Mode].capMul, modes[Mode].spots[bucket].{bias,trust,n,saved,diedAfterAssist,pessimism}

## BakeMap JSON

levelKey, rev, tps (runtime-measured for brute), method, sourceReplay, clicks[{nominalFrame,earliest,latest,down,player2,x,mode}]

Bake runs serial on the main thread via BakeScheduler (no worker pool).

## Reentrancy

While simulating: no sounds/achievements/percentage/particles/trigger side-effects; SimCollisionPolicy filter; orb hold-gate.
Safe mode + assistTouchedRun block percentage/newbest when assist injected.

Owned ghosts (shadow + snapshots) never enter vanilla `destroyPlayer` / `playerDestroyed` (incl. anticheatSpike).
Death to an owned ghost as the killer object is swallowed. Real-player deaths are swallowed while
`m_simulating` (anticheat on real players still passes). During sim, `collisionCheckObjects` runs
only for the shadow. Live collision lists strip owned ghosts. Active shadow is parked off-level
after each sim (wave trail reset); ghost `update` is no-op unless actively stepping.

## Constants

clusterGapX=50, windowMarginK=1, tickBudget=4000, sim-budget-us=1500. Platformer: assist off.

## Authorship

Design requirements: maintainer. Spec text and implementation: AI-assisted (see repository README).
