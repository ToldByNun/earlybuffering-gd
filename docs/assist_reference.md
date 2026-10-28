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

`SelfBakeRecorder` records vanilla jump inputs (never assist injects). On death / checkpoint / complete / reset, merges into `BakeMap` via `mergeSelfClicks` (`method=self` when empty/self; dedup union; cap 512). **Death and reset never refine** (merge-only — refine on the main thread caused 1–2s freezes). Checkpoint / complete / quit may refine when enabled, start snapshot exists, and assist did not touch the run (`refineSelfWindows`, budget 64 sims). Quit cancels in-flight refine.

## Trigger World Bake

Settings (Trigger World): `trigger-bake` (default on), `trigger-bake-skip-dynamic` (default on).

When enabled, static move/rotate/scale trigger motion is cached per level content key (`triggers/{fullKey}.json` under the mod save dir). On level enter: `TriggerWorldStore::loadForLevel` if present (miss = empty store until baker). Shadow sim applies baked poses so static movers are not `UnknownOnOverlap` (later commits). Player-coupled dynamics stay out of the static cache; with `trigger-bake-skip-dynamic` they stay Unknown. Off `trigger-bake` = clear store / legacy Unknown-on-overlap.

JSON v1: `{ version, key, tps, tracks: [{ uniqueId, objectId, kind, ready, keys: [{ t, x, y, r, sx, sy, a }] }] }`. Caps: 2048 tracks, 8192 keys/track. Save on quit/complete when dirty (not on death).

Classifier (`TriggerClassify`): **only lethal-motion candidates** — Move/Rotate/Scale/Spawn/Follow (901/1346/2067/1268/1347/1585/1817). Camera/pulse/song/etc. → always `Ignore`.

**Static vs player-dynamic:** Following **another group/object** (e.g. group 6 follows group 5 that moved +10/−100) is `StaticMove` — the chain is deterministic and bakeable. Only motion that depends on the **live player** is `DynamicFollow` (lock-to-player, rotate-follow P1/P2, target-player flags, Follow Player Y `1585`, lock-to-camera on movers). Touch-triggered motion start → `DynamicOther`. Unknown effect IDs → `Ignore`. `TriggerClassify::selfCheck` runs at mod load.

## Not in scope

- Late-forgiveness (rewind time for late clicks)
- Enabling raw `triggerObject` during live plan (mutates the real level)
- Multi-click injection queue beyond Smart d0 selection

## Pause / settings UI

Pause: optional status lines (`show-pause-bake`) + top-right **EB** (`show-pause-hub`). No side button stacks.

EB opens the normal Geode ModSettingsPopup with a page switcher:

| Page | Contents |
|------|----------|
| Main | Enabled, Preset, Safe Mode, Debug |
| Bake | Native rows (`eb-action`): Import Bake Replay (button = `import` or `{name.ext}`), Cycle Mode, Bake Map, Brute Bake, Cancel |
| Ingame | Overlay, pause status/hub toggles |
| Assist | Action rows (Cycle Preset / Reset Learn / Export·Import) + Scope/Window/Algorithm/Smart/Learning |
| Visual | Trajectory, Save Feedback / Color |
| Modes | Cube…Swing per-mode blocks |
| Keys | Hotkeys + Internal |

Action rows use custom type `custom:eb-action`: same chrome as bool settings (label + info `i`), Geode button on the right instead of a checkbox.

## Settings table

Geode setting titles (within pages): **General** → **Assist Scope** (+ Off Ranges) → **Default Window** → **Algorithm** → **Smart Prediction** → **Learning** → per-mode → **Keybinds** → **Internal**.

| Key | Type | Default | Min | Max | Group / notes |
|-----|------|---------|-----|-----|-------|
| enabled | bool | true | | | Main — master |
| preset | string | Tight | Tight/Normal/Wide/Max/Off | | Main — fills defaults |
| safe-mode | bool | true | | | Main — block saves when assist enabled |
| debug-logging | bool | true | | | Main |
| perf-trace | bool | true | | | Main — section timings; spikes → console + `earlybuffer-perf.log` in mod save dir |
| show-overlay | bool | true | | | Ingame |
| show-trajectory | bool | false | | | Visual |
| show-pause-bake | bool | true | | | Ingame — pause status lines only |
| show-pause-assist | bool | false | | | Ingame — legacy unused (actions under Assist page) |
| show-pause-hub | bool | true | | | Ingame — EB button |
| save-feedback-sound | bool | true | | | Visual — per-tick MP3 cue on delayed inject |
| save-feedback-color | bool | true | | | Visual — flash player icon on delayed inject |
| save-feedback-counter | bool | false | | | Visual — world circle + +N at click (clears on death/restart); palette 1…8 `#ff614e`…`#c069fa`; cap 64 markers |
| save-icon-color | rgb | #7CFF4A | | | Visual — flash color when save-feedback-color is on |
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
| robust-k | int | 0 | 0 | 3 | Smart — 0=auto (mode/speed/dt var → 1–3) |
| robust-jitter | bool | false | | | Smart — re-sim top cand at firstDt ±3% |
| auto-tune | bool | true | | | Learning — never changes ticks; no trust-up from inject saves |
| self-bake | bool | true | | | Learning — record vanilla clicks in practice |
| self-bake-normal | bool | false | | | Learning — also record outside practice |
| self-bake-refine | bool | true | | | Learning — budgeted window search on flush |
| trigger-bake | bool | true | | | Trigger World — static mover cache for shadow sim |
| trigger-bake-skip-dynamic | bool | true | | | Trigger World — do not bake/probe follow-player etc. |
| assist-verify | string | off | off/ab | | Learning — debug A/B only |
| MODE-enable/overwrite/ticks/cap/slack/latency | mixed | see mod.json | | | Per-mode cube..swing; latencyWeight defaults Cube 0.25 … Wave 1.0 |
| stage-index | int | 0 | 0 | 4 | Internal — preset mirror |
| swallow-hopeless | bool | false | | | Internal — deprecated unused |

## Pseudocode plan()

1. tickBudget from opts (default 4000); if verifyAb then *2
2. modelFor; capture state
3. d0 press baseline -> ThreatZone clusterGapX=50
4. **no-threat Pass**: baseline did not die, zone invalid, no orb/pad hint → Pass (skip further cands)
4b. **wave-hazard-proximity Pass**: Wave + hazard AABB within ahead proximity (~70–140px) + no orb/pad → Pass even if a distant ThreatZone exists (avoids phase-shift “assists” in zigzag spam)
4c. After cand eval: **wave-futile-delay Pass** if Wave, no orbIntent, all surviveBin ≤0 and best endX gain &lt; 8 → Pass
5. eval delays under tick + sim-budget-us
6. orbIntent: earliest +/-k robust activation (NO midpoint); optional firstDt ±3% jitter on top cand
7. else d0 clearsThreat -> Pass; else earliest +/-k robust viable
8. budgetHit -> Pass; linked dual both clear

Lexico: orbUseful > surviveBin > endX−λ·delay > earliest. Bins 24/48/96. k from `robust-k` or auto (mode/speed/dtVariance). λ = ModeParams.latencyWeight.

## Click intent (spam) v2

`ClickIntentGuard` ring of recent Jump presses per player (P1/P2 independent).

Pass when **density** (wave ≈ 3 / 10 ticks) **and** (**rhythmic** gaps: mean ≤ mode max, low σ, no mid-spam pause) **or** **very dense** (need+2). First 1–2 clicks and pause-then-resume → assist allowed.

**Do not Pass** (allow assist) when spatial orb/pad ahead **or** a BakeMap window is nearby. Portal/teleport corridor spam looks rhythmic → Pass unless orb/pad.

Continuous modes also Pass release assist during spam. Reset on attempt / quit. Self-check: `ClickIntentGuard::selfCheck`.

## Divergence / UNTRUST

`DivergenceTracker` ring (48): bad if delayed assist died, or orb expected but missed (death or delayed-for-orb). Warn ≥35%/n≥8; UNTRUST ≥45%/n≥12. `pollOutcome` uses real `planDelay`; `onRealDeath` notes pending outcomes. Live/Hybrid-live: `simUntrusted` → force Pass + overlay `UNTRUST pass` (no explore). `shouldWarn` soft: `capMul *= 0.85`. Clear on attempt reset only.

## effectTick attribution

`onPhysicsTick` increments `attemptTick`, then `beginRealUpdate` → `PlayerObject::update` → `endRealUpdate`.
CBF / mid-step clicks happen while `m_inRealUpdate` is true (`midStep`).

| Mode class | midStep | between frames | firstDt |
|------------|---------|----------------|---------|
| Continuous (Wave/Ship/Swing/Ufo/Spider) | `attemptTick` | `attemptTick` (never +1) | mid: remainder/`timeWarp`; else full `simDt` |
| Discrete (Cube/Ball/Robot) | `attemptTick` | `attemptTick+1` | same |

Rule: never fold CBF remainder into the tick index — that caused systematic ±1 between mid-step and between-frame presses (wave spam). Helpers: `AssistController::resolveEffectTick` / `effectTickSelfCheck`. Debug: `attrib:` AssistLog line + PerfTrace `attrib` when debug-logging or perf-trace is on.

## Hold / release after assist (7.5)

After a **Delay** press is chosen, the planner probes release-after values `{0,1,2,3,5,8,10,14,18,24,32,40}` (budget ≤8 extra sims) and records:

| Field | Meaning |
|-------|---------|
| `releaseEarliest`…`releaseLatest` | Safe hold durations (ticks after press) that still clear the threat |
| `releaseAfter` | Preferred release (continuous → earliest; tap → mid-window) |

On inject: if the user still holds, arm `pendingRelease` at preferred; continuous modes may hold until `releaseLatest`, then force release. Early user release inside the window → no inject. **Re-click** while pending → cancel scheduled release and re-plan (second click in the window). Dash `requireHold` keeps release deferred while `m_isDashing`.

Branch releases `{2,5,10,18}` and dash-hold `40` seed the window when they already won viability.

## fireTick vs jumpTick

When arming a Delay press, `planDelay = plan.delay` and `expectedJumpTick = effectTick + jumpTick` (sim-relative).

`resolveFireTick(effectTick, delay, jumpTick, requireOrb)`:

- Non-orb and `jumpTick > delay` → `fireTick = effectTick + jumpTick` (inject when the jump matters, not one frame early).
- Orb / `jumpTick <= delay` → `fireTick = effectTick + delay` (activation window wins; hold covers lag).
- Always clamp to `pressTick + limit`.

`shouldFireBuffered`: fire when `attemptTick >= fireTick`, or on the last tick of the window if `planDelay` is already met (late-fire guard). SaveFeedback `+N` uses actual `attemptTick - pressTick` at inject, not `planDelay`.

Helpers: `AssistController::resolveFireTick` / `shouldFireBuffered` / `fireTickSelfCheck`.

## Recheck slide safety

Before inject, live recheck may slide `fireTick`. Rules (`acceptRecheckSlide`):

- Plan must be Delay, `delay > 0`, `!budgetHit`, and (`threatCleared` or orb).
- Cap late slides to `oldFire + 2` (non-orb); orb may wait for `dMin`.
- Never past `absMax - robustK` (collapsed margin falls back to `absMax`).
- Clamp to `pressTick + limit`. Early slides (earlier fire) allowed.
- Log `recheck: oldFire→newFire` via AssistLog + PerfTrace.
- CPS / mode-change force-fire: `allowRecheck=false` (inject immediately).

## Pseudocode fireBuffered()

Wait for dMin if requireOrb; up to 2 safe reclamps into [dMin,dMax] (recheck also re-aligns jumpTick); inject; PendingRelease if needed.
Press B while ArmedPress: fire A first, then plan B.

## Buffer states

Idle | ArmedPress | ArmedRelease | PendingRelease
Cancel: death, resetLevel, quit, checkpoint load. Force-fire: mode-change, CPS unlock.

## Smart bake-chain

When `smartDepth > 1` and BakeMap has later clicks: `planSequence` scores each allowed `d0` with a fixed follow-up timeline from the next 1–2 bake/self-bake clicks (same mode; beyond horizon / mode change aborts). Score = survive + reach next X − weak latency. Only `d0` is buffered. No bake follow-ups → prior hypothetical d1 grid.

## Live bake-prior (prefer band)

`PlanOptions.preferDelayMin/Max` from a nearby BakeMap window (delay ticks). Cand order: d0 → orb window → prefer±k → rest. Early-exit after prefer margin if a ±k-robust prefer delay is viable (`!budgetHit`). Orb-intent still wins final pick over prefer. Empty / out-of-limit band ignored.

## Adaptive robustK + latency (+ wave spam)

`robust-k` 0 → auto (`ShadowSimulator::autoRobustK`): Wave/Ship/Swing start at 2; speed >1.1 → ≥2, >1.5 → 3; relative dt EMA >0.08 bumps +1 (cap 3). **Wave + spam-dense** (ClickIntentGuard density, even when orb forces assist): keep ≥2; if speed >1.05 → **3**. Fixed 1–3 overrides auto. `robust-jitter`: after pick, re-run with firstDt×0.97/1.03; fail → mark non-viable and re-pick.

`ModeParams.latencyWeight` (defaults Cube 0.25 … Wave 1.0) enters `betterCand` as `endX − λ·delay`. On Wave spam-dense assists only, λ is scaled ×0.65 so earliest-bias does not phase-shift corridors.

**ticks-wave default stays 2** (Tight feel, not autoplay). With auto k=2..3, full ±k neighbors often do not fit in a 2-tick window — `pickRobust` falls back to earliest viable. Use Normal/Wide/Max (wave 4/8/8) when you want real ±k margin in spam. Max uses ticks-all=16 but ticks-wave=8 on purpose (wave windows stay shorter than cube).

## LevelProfile JSON

key (LevelContentKey fullKey with content rev), modes[Mode].capMul, modes[Mode].spots[bucket].{bias,trust,n,saved,diedAfterAssist,pessimism,histN,earlyHist[17]}

Earliness histogram bins −8…+8 from Pass/postmortem only (never inject saves). Laplace modal → delay offset when histN≥3; else classic bias±2. Bin decay on autosave.

**Histogram ±1 explore gate** (`AssistLearning::allowHistogramExplore`): while `histN < 8`, a ~20% roll may add ±1 to delay — **blocked** for Wave/Ship/Swing, when `ClickIntentGuard` density would Pass, when a bake-prefer band is active, or when `diedAfterAssist > 0` at that spot. Preferred delay offset (non-random) still applies.

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
after each sim (wave trail reset + `removeObjectFromSection`); ghost `update` is no-op unless actively stepping.
Before `resetLevel` / checkpoint load: `evacuateGhosts` pulls all owned ghosts out of section vectors (avoids `sortSectionVector` AV) and cancels an in-flight bake.

## Constants

clusterGapX=50, windowMarginK=1, tickBudget=4000, sim-budget-us=1500. Platformer: assist off.

## Authorship

Design requirements: maintainer. Spec text and implementation: AI-assisted (see repository README).
