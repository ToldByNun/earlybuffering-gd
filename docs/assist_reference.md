# EarlyBuffer Assist Reference

Rebuild / harden guide for Geometry Dash 2.2081 (Geode). Physics unit = tick (measured TPS, often ~240/s). Threat zones use world X.

## Modes (honest)

| Mode | Role |
|------|------|
| Live | Input forgiveness: shadow-plan each click; Delay buffers, Pass = vanilla. |
| Hybrid | Bake window if near + live verify clearsThreat, else Live. |
| Replay | Precomputed click windows from imported macro bake (TAS-like timing map). |
| BruteForce | Precomputed exhaustive safe timings (TAS-like generator). Not a live multi-click bot queue. |

Live/Hybrid never block hopeless clicks. Replay/Brute are precomputed windows, not a live macro injector queue.

## Bake verify (Hybrid / Replay / Brute)

`BakeMap::findNear` prefers `BakedClick.mode == kindOf(player)` within 120px X; falls back to any mode if none match.
Before using a window, `BakeVerifier` runs one `ShadowSimulator::verifyDelay` (d0 zone + runOne at planned delay). Success = clearsThreat and not diedBeforeAction (linked dual checked). Fail: Hybrid → Live; Replay/Brute → Pass.

## Self-bake

Settings (Learning): `self-bake` (default on, practice), `self-bake-normal` (default off), `self-bake-refine` (default on).

`SelfBakeRecorder` records vanilla jump inputs (never assist injects). On death / checkpoint / complete / reset, merges into `BakeMap` via `mergeSelfClicks` (`method=self` when empty/self; dedup union; cap 512). When refine is on, start snapshot exists, and assist did not touch the run: `refineSelfWindows` binary-searches earliest/latest (budget 64 sims, bake-radius, cancelable). Quit cancels refine and merges point windows only.

## Not in scope

- Late-forgiveness (rewind time for late clicks)
- Full move/rotate/scale trigger simulation (Unknown/warn only)
- Multi-click injection queue beyond Smart d0 selection

## Settings table

Geode UI order (titles): **General** → **Assist Scope** (+ Off Ranges) → **Default Window** → **Algorithm** → **Smart Prediction** → **Learning** → per-mode → **Keybinds** → **Internal** (`stage-index`, deprecated `swallow-hopeless`).

| Key | Type | Default | Min | Max | Group / notes |
|-----|------|---------|-----|-----|-------|
| enabled | bool | true | | | General — master |
| preset | string | Tight | Tight/Normal/Wide/Max/Off | | General — fills defaults |
| safe-mode | bool | true | | | General — block saves when assist enabled |
| show-overlay | bool | true | | | General |
| debug-logging | bool | true | | | General |
| show-trajectory | bool | false | | | General |
| show-pause-bake | bool | true | | | General |
| show-pause-assist | bool | true | | | General — hotkey mirrors + config I/O |
| save-feedback | string | both | off/icon/sound/both | | General — sound = per-tick MP3; icon = color flash |
| save-icon-color | rgb | #7CFF4A | | | General — flash color for icon/both |
| assist-only-practice | bool | false | | | Scope |
| assist-startpos-only | bool | false | | | Scope |
| assist-percent-min | int | 0 | 0 | 100 | Scope |
| assist-percent-max | int | 100 | 0 | 100 | Scope |
| assist-off-N-from/to | float | -1 | -1 | 100 | Scope — N=1..6; -1 unused; inclusive off range |
| ticks-all | int | 2 | 0 | 32 | Window |
| ticks-wave | int | 2 | 0 | 32 | Window |
| death-slack | int | 2 | 0 | 16 | Algorithm — look-ahead slack, not zone |
| look-ahead-cap | int | 0 | 0 | 240 | Algorithm — 0=auto |
| assist-mode | string | Hybrid | Live/Replay/Hybrid/BruteForce | | Algorithm |
| bake-radius | int | 0 | 0 | 128 | Algorithm — 0=auto |
| cps-limit | int | 0 | 0 | 40 | Algorithm — 0=off |
| min-click-interval-ms | int | 0 | 0 | 500 | Algorithm — skip assist only |
| assist-orbs | bool | true | | | Algorithm |
| sim-budget-us | int | 1500 | 0 | 10000 | Algorithm — soft wall-clock; 0=unlimited |
| tick-budget | int | 4000 | 500 | 20000 | Algorithm — hard tick sum; exhaust=Pass |
| assist-press | bool | true | | | Algorithm |
| assist-release | bool | true | | | Algorithm |
| smart-depth | int | 1 | 1 | 3 | Smart |
| smart-combos | int | 48 | 8 | 256 | Smart |
| smart-horizon | int | 0 | 0 | 320 | Smart — 0=auto |
| auto-tune | bool | true | | | Learning — never changes ticks; no trust-up from inject saves |
| self-bake | bool | true | | | Learning — record vanilla clicks in practice |
| self-bake-normal | bool | false | | | Learning — also record outside practice |
| self-bake-refine | bool | true | | | Learning — budgeted window search on flush |
| assist-verify | string | off | off/ab | | Learning — debug A/B only |
| MODE-enable/overwrite/ticks/cap/slack | mixed | see mod.json | | | Per-mode cube..swing |
| stage-index | int | 0 | 0 | 4 | Internal — preset mirror |
| swallow-hopeless | bool | false | | | Internal — deprecated unused |

## Pseudocode plan()

1. tickBudget from opts (default 4000); if verifyAb then *2
2. modelFor; capture state
3. d0 press baseline -> ThreatZone clusterGapX=50
4. **no-threat Pass**: baseline did not die, zone invalid, no orb/pad hint → Pass (skip further cands)
5. eval delays under tick + sim-budget-us
6. orbIntent: earliest +/-k robust activation (NO midpoint)
7. else d0 clearsThreat -> Pass; else earliest +/-k robust viable
8. budgetHit -> Pass; linked dual both clear

Lexico: orbUseful > surviveBin > endX > earliest. Bins 24/48/96. k=1.

## Click intent (spam)

`ClickIntentGuard` ring of recent Jump presses per player. Dense spam (wave ≈ 3 presses / 10 ticks) → assist Pass unless spatial orb/pad ahead. Continuous modes also Pass release assist during spam. Reset on attempt / quit.

## Divergence / UNTRUST

`DivergenceTracker` ring (48): bad if delayed assist died, or orb expected but missed (death or delayed-for-orb). Warn ≥35%/n≥8; UNTRUST ≥45%/n≥12. `pollOutcome` uses real `planDelay`; `onRealDeath` notes pending outcomes. Live/Hybrid-live: `simUntrusted` → force Pass + overlay `UNTRUST pass` (no explore). `shouldWarn` soft: `capMul *= 0.85`. Clear on attempt reset only.

## Pseudocode fireBuffered()

Wait for dMin if requireOrb; up to 2 reclamps into [dMin,dMax]; inject; PendingRelease if needed.
Press B while ArmedPress: fire A first, then plan B.

## Buffer states

Idle | ArmedPress | ArmedRelease | PendingRelease
Cancel: death, resetLevel, quit, checkpoint load. Force-fire: mode-change, CPS unlock.

## Smart bake-chain

When `smartDepth > 1` and BakeMap has later clicks: `planSequence` scores each allowed `d0` with a fixed follow-up timeline from the next 1–2 bake/self-bake clicks (same mode; beyond horizon / mode change aborts). Score = survive + reach next X − weak latency. Only `d0` is buffered. No bake follow-ups → prior hypothetical d1 grid.

## LevelProfile JSON

key (LevelContentKey fullKey with content rev), modes[Mode].capMul, modes[Mode].spots[bucket].{bias,trust,n,saved,diedAfterAssist,pessimism,histN,earlyHist[17]}

Earliness histogram bins −8…+8 from Pass/postmortem only (never inject saves). Laplace modal → delay offset when histN≥3; else classic bias±2. Bin decay on autosave; explore while histN<8.

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
