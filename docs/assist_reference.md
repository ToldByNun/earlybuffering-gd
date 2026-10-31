# EarlyBuffer Assist Reference

Planner / settings contract for **EarlyBuffer v1.3.2** on Geometry Dash **2.2081** (Geode). Physics unit = tick (measured TPS, often ~240/s). Threat zones use world X. **Last 2.2081 release line** — see repository README.

## Modes (honest)

| Mode | Role |
|------|------|
| Live | Input forgiveness: shadow-plan each click; Delay buffers, Pass = vanilla. |
| Hybrid | Bake window if near + live verify clearsThreat, else Live (+ foresight / miss-assist). |
| Replay | Precomputed click windows from imported macro bake (TAS-like timing map). |
| BruteForce | Precomputed exhaustive safe timings (TAS-like generator). Not a live multi-click bot queue. |

Live/Hybrid never block hopeless clicks. Replay/Brute are precomputed windows, not a live macro injector queue.

## Bake verify (Hybrid / Replay / Brute)

`BakeMap::findNear` prefers `BakedClick.mode == kindOf(player)` within 120px X; falls back to any mode if none match.
Before using a window, `BakeVerifier` runs one `ShadowSimulator::verifyDelay` (d0 zone probe + runOne at planned delay; **`delay==0` reuses the d0 runOne** — no second identical sim). Success = clearsThreat and not diedBeforeAction (linked dual still checked once). Budget/wall exhaustion → `budgetHit` (not ok). **Never** soft-trust a bake on `budgetHit`.

**Hybrid shared press budget:** bake verify + Live fallthrough share one wall/tick envelope (`PressSimBudget` = `sim-budget-us` / `tick-budget`). Verify draws first but **leaves a Live reserve** (500µs / 800 ticks); Live gets the remainder — not a second full press plan. **Remainder `usLeft==0` ≠ unlimited** (`PlanOptions.budgetUs<=0` only when the user set `sim-budget-us=0`). Missing / no-window / past-latest skip verify (Live keeps the full envelope). Live arm `verifyDelay` and HUS keep their smaller caps. Replay/Brute keep independent verify caps and Pass-on-fail.

**Hybrid verify→Live hitch:** when verify ran and rejected the window, Live must not re-enable assist via `nearBake` spam override or prefer-band from that window (that path hitch-stuttered around moderate CPS, e.g. ~5). Live is also capped to ≤½ press tick/wall on that fallthrough; exit-predict skips if wall remainder &lt; 600µs. Still never soft-trust `budgetHit` bake; orb foresight / HUS unchanged.

**Hybrid reuse:** `tryBakeAssist` copies findNear window fields into `HybridPressReuse` (no bake-vector pointers) and, on verify fail, the verify `threatZone`. Live uses the copy for near-bake / prefer-band (when verify did not reject) and seeds `PlanOptions.seedThreatZone` so `plan()` skips `buildThreatZone`. Missing / no-window → empty reuse.

**HybridVerifyCache:** within one attempt, memoizes Hybrid `BakeVerifier` results keyed by window fields (`nominalFrame` / earliest / latest / down / p2 / mode) + delay + limit — **no bake-vector pointers**. Hit skips re-sim when player X is within 8px. `budgetHit` results are not cached (envelope is per-press). Cleared on attempt reset and death.

**Hybrid LagProbe / overlay:** with **Lag Probe** on, press path writes standalone lines (no death episode needed):

```
HYBRID eb.hybrid.verify = 0.40ms | d=3 lim=16 cache=0
HYBRID eb.hybrid.live   = 1.10ms | vfy=1 remTick=2800 remUs=500
```

`cache=1` means attempt memo hit. Overlay (In-Level Overlay on): Hybrid shows `hy bake [lo,hi]` on bake success or `hy verify … → live` / `hy bake missing → live` on fallthrough.

| Mode | Verify fail / past window / missing bake |
|------|------------------------------------------|
| Hybrid | Fall through to **Live** (`TravelForesight` + OpportunityProbe / HUS / idleSafer). Overlay hint `… → live`. No bake Pass verdict. |
| Replay / Brute | Pass with reason: `pass-bake-verify-fail`, `pass-bake-past-latest`, `pass-bake-missing`, `pass-bake-no-window`. |

Early-before-window still buffers until bake earliest (clamped to `limit`).

**Shared-budget Hybrid policy (not soft-trust):** a bake is used only when `BakeVerifier` returns ok (`clearsThreat`, not `budgetHit`). Exhausted verify never injects the bake window “on faith.” Live fallthrough always keeps a reserve / remainder under the same press envelope; `usLeft==0` is a tight exit, not unlimited wall. Hitch path after reject: no near-bake spam override, no prefer-band from that window, ≤½ press tick/wall, skip exit-predict if remUs &lt; 600.

## PlatformLimits (Android + iOS)

`PlatformLimits::isConstrainedDevice()` is true on **Android and iOS** only (Windows / macOS passthrough). Same ceilings as the prior Android-only policy:

| Limit | Constrained | Desktop |
|-------|-------------|---------|
| `sim-budget-us` clamp | ≤2000 (0 stays unlimited) | as set |
| `tick-budget` clamp | ≤2000 | as set |
| Overlay top inset | 28 | 10 |
| Disk PlanGold on `$on_mod(Loaded)` | no (`allowLoadDiskSelfCheck` false) | yes |

Mobile I/O (Config / replay-drop / Dev report) uses `SaveDropPaths` + path-on-clipboard alerts — see Config section and `INSTALL.md` (iOS smoke + `--require ios`).

## Self-bake

Settings (Learning): `auto-tune` (default off), `self-bake` (default off, practice), `self-bake-normal` (default off), `self-bake-refine` (default on when self-bake is enabled).

`SelfBakeRecorder` records vanilla jump inputs (never assist injects). On death / checkpoint / complete / reset, merges into `BakeMap` via `mergeSelfClicks` (`method=self` when empty/self; dedup union; cap 512). **Death and reset never refine** (merge-only — refine on the main thread caused 1–2s freezes). Checkpoint / complete / quit may refine when enabled, start snapshot exists, and assist did not touch the run (`refineSelfWindows`, budget 64 sims). Quit cancels in-flight refine.

**Replay / Brute bake:** attempt-start pose lives in reserved snapshot slot 0 (`captureStartSnapshot`) so DecisionLog / trajectory cannot clobber it. Offline bake remaps Trigger World with bake `dt`, not live hitch `simDt`. Replay macros at non-game FPS (360 / physics-bypass 3000, …) are **resampled** to live TPS (~240); zero-length clicks after remap get a **minimum 1-tick hold**. Bake applies **downs → physics → ups** each frame so same-frame press+release still holds for that tick (clickbots). Majority-P2 MegaHack exports → P1; dual P2 clicks are dropped from the P1 timeline. Real P1/P2 are parked off-map during bake; sim collision ignores PlayerObjects. Offline bake trusts current toggle pose (no Unknown abort). Empty → error + `diag/bakefail_*`. Always **reset → pause → Bake Map**. Diag: Dev → Dump All Diag.

## Trigger World Bake

Settings (Trigger World): `trigger-bake` (default on), `trigger-bake-skip-dynamic` (default on).

When enabled, static move/rotate/scale/toggle motion is cached per level content key (`triggers/{fullKey}.json` under the mod save dir). Overlay shows `trigger-world: …`.

**Lifecycle**

| Event | Trigger world |
|-------|----------------|
| Level enter (`onLevelInit`) | `TriggerWorldBaker::loadOrBuild` — load JSON; on miss queue budgeted `tickSlice` build |
| Physics tick | `tickSlice` (~750µs); sim apply uses `attemptStartTick + t` |
| Checkpoint | **Keep** store + baker; reset `attemptTick` only (no rebuild) |
| Death | Cancel in-flight slices (no disk flush); `resetAttempt` **resumes** build if store still empty |
| Complete | `save` if dirty (no clear) |
| Quit | Cancel → `save` if dirty → `clear` |
| `trigger-bake` off | Clear store; no build |

**Sim collision:** ready Static* track + apply this step → `Include` only while pose active (`writePose` clears visibility/`poweredOn` when inactive → `Skip`). **Toggle orb 1594 / touch-1049 targets:** pre-flip → `UnknownOnOverlap` (`pass-toggle-unknown`); after `ToggleOrbSim` models the hit → `Include`/`Skip` by post-flip active (no vanilla `triggerObject`). DynamicFollow targets: skip-dynamic (default) → `UnknownOnOverlap`; skip off → cheap live probe then `Include`. Dual / rotate-follow stay Unknown. **Follow-player never enters static JSON.**

JSON v1: `{ version, key, tps, tracks: [{ uniqueId, objectId, kind, ready, keys: [{ t, x, y, r, sx, sy, a }] }] }`. Softcap ≥100 000 tracks / 32 768 keys/track (AssistLog + overlay `tw softcap`); hard OOM valve 250 000 / 65 536 with loud truncate — **no silent 2048 drop**. Baker collect finishes every object-layer child across slices, then scans `m_sections` for extras; spawn-timed StaticMove/Rotate/Scale/Toggle (linear); group-follow / touch / toggle-orb / enter-X skipped. Long movers decimate keys (keep endpoints) under soft instead of dropping the track.

`ShadowSimulator::beginCapture` snapshots ready Static* (+ dynamic targets for restore). `applyTriggerWorldAtTick` / optional `applyLiveDynamicNear` before collide — **near ghost X first** (~±420), capped (~96 objects / ~350µs); far tracks hold last pose. LagProbe: `eb.triggerBake.load|build|save`, `eb.trigger.capture|apply|restore`, `triggerWorld.checkpoint`.

Classifier: Move/Rotate/Scale/Spawn/Toggle/ToggleOrb/Follow (901/1346/2067/1268/**1049**/**1594**/1347/1585/1817). Camera/pulse/song → `Ignore`. Group-follow → `StaticMove`; player-coupled → `DynamicFollow`; touch → `DynamicOther`.

**Self-check / manual matrix:** `TriggerWorldStore::selfCheck` + `TriggerClassify::selfCheck` run at mod load (and PlanGold). With `debug-logging`, the banner lists edge cases: static corridor, follow-player Unknown, cache-hit 2nd enter, skip-dynamic on, quit mid-build, death/reset LagProbe without `eb.triggerBake.build` inside the death episode.

## Not in scope

- Late-forgiveness (rewind time for late clicks)
- Enabling raw `triggerObject` during live plan (mutates the real level)
- Multi-click injection queue beyond Smart d0 selection
- Perfect spawn-DAG / every 2.2 trigger flavour in the static baker (unknown statics stay DynamicOther / Unknown until supported)

## Pause / settings UI

Pause: optional status lines (`show-pause-bake` / Pause Status Lines) + top-right **EB** (`show-pause-hub`). No side button stacks.

**EB** (and Geode → Mods → EarlyBuffer → Settings) opens the normal Geode `ModSettingsPopup`. EarlyBuffer attaches a **text-only category strip** under the search field (selected = white, idle = grey; no pill / background chrome). Tapping a label filters the scroll list to that page; Geode’s search still runs on the visible rows.

| Tab | Contents |
|-----|----------|
| Main | Enabled, Safe Mode, Assist Mode, **Window** (`window-unit` / `ticks-all` / `ticks-wave`), overlay / pause chrome, light diagnostics (`debug-logging`, `perf-trace`, `lag-probe`) |
| Scope | Practice / start-pos / percent window / off-ranges |
| Assist | Action rows (Reset Learn / Export·Import Config) + Algorithm / Smart / Learning / Trigger World |
| Bake | Import Bake Replay, Cycle Assist Mode, Bake Map, Brute Bake, Cancel, Bake Radius |
| Modes | Cube…Swing per-mode blocks |
| Visual | Trajectory, Bake Visualizer, Save Feedback / Color |
| Keys | Hotkeys (no Preset / F6 cycle) |
| Dev | Dev Lab session / eval / overlay / actions |

Every `mod.json` key (including `title-*` and `custom:eb-action`) maps to exactly one tab via `InLevelSettingsFilter` — there is no Assist catch-all dump. Action rows use `custom:eb-action`: same chrome as bool settings (label + info `i`), Geode button on the right instead of a checkbox.

## Dev Lab diagnostics (session JSON)

With **Dev Record** on, each session writes `devtraces/session-*.json` under the mod save dir.

| Field | Where | Meaning |
|-------|--------|---------|
| `settingsFingerprint` | session root | Compact stage/mode/pick/ticks/safe/orbs/budgets/trigger-bake/learning summary (always filled on session start / level meta refresh) |
| `witness.triggerReadyTracks` / `triggerTrackCount` | `planFinished` | Store ready vs total tracks |
| `witness.triggerBakeEffects` / `triggerBakeChildren` / `triggerCollectedEffects` | `planFinished` | Last bake: effects that produced keys, object-layer children, collected EffectGameObjects |
| `witness.triggerSkipFollow` / `SkipDynamic` / `SkipNonSpawn` | `planFinished` | Last bake skip counters |
| `witness.triggerCanSpool` | `planFinished` | `true` only if ready tracks exist |
| `armVerifyOk` / `budgetHitAtArm` | DelayArm (+ copied on Fire/Outcome) | Live verify passed before arm; plan reported budgetHit |
| `recheckDelta` | Recheck / Fire / Outcome | Fire-tick slide (per recheck or sum on later events) |
| `holdExtendMax` | DelayArm / Fire / Outcome | Post-release hold-extend cap when armed |
| `deathAfterFire` / `ticksToDeath` | Outcome | Death after a real assisted fire; ticks from fire→death |
| Dev Eval `mismatch liveDelay=N` | overlay / report | Live planned Delay &gt; 0 but eval `bestSurv=none` |

Overlay compact Trigger-World: `tw empty` / `tw N/M` / `tw building %`. LagProbe mark `eb.assist.halluc` on death-after-fire (detail includes press/fire/ticksToDeath).

## Config export / import (`ConfigBundle`)

Assist → **Export Config** / **Import Config** (also Win32 file picker; non-Win uses `earlybuffer-config.json` under the Geode mod save dir via `SaveDropPaths`). Mobile alerts show the **full path**, copy it to the **clipboard** (best-effort), and use Files / Share / file manager / Finder-via-device wording (no ADB-only steps). Dev Copy Report dual-writes report text + path on the clipboard when the file save succeeds.

### v2 schema

```json
{
  "version": 2,
  "settings": {
    "enabled": true,
    "ticks-all": 4,
    "ticks-wave": 4,
    "look-ahead-cap": 0,
    "save-icon-color": "#7CFF4A",
    "hotkey-debug": "Ctrl+Shift+D"
  }
}
```

- **`version`**: `2` (current).
- **`settings`**: all **124** portable Geode keys from `mod.json` (bool / int / float / string / color / keybind). Excludes `title-*` and `custom:eb-action` rows.
- **Color**: `"#RRGGBB"` hex.
- **Keybind**: human string (`"F6"`, `"Ctrl+Shift+D"`); multiple binds joined with `" | "`. JSON string arrays also accepted on import.
- Alerts show `version` / `applied|keys` / `skipped` and the file path.
- **Export** reads the committed Geode store only — uncommitted edits still open in the settings UI are not exported.

### Removed keys (old JSON)

These keys were deleted from `mod.json` (unused / deprecated). If an older export still contains them, import **skips** them and applies every other known key:

| Key | Why gone |
|-----|----------|
| `preset` / `stage-index` / `hotkey-cycle` | Preset packs removed — use Main → Window |
| `show-pause-assist` | Unused legacy pause toggle |
| `swallow-hopeless` | Deprecated; unused |
| `dev-record-include-pass` | Cached in DevConfig, never read by recorder |
| `dev-eval-budget-us` | Cached in DevConfig, never read by eval |

Any other unknown key / wrong type is skipped the same way (`skipped` count in the alert).

### v1 compatibility

Older flat files (`{ "version": 1, "preset": "...", "ticks-all": 2, ... }` without nested `settings`) still import: known keys apply, unknown keys ignored.

### Merge semantics

- Missing key → leave the live setting unchanged.
- Wrong JSON type / bad hex / bad keybind → **skip that key** (no `0`/`false` coercion into live settings).
- Import runs under `StageConfig::beginApplying` / `endApplying` so overlay listeners stay quiet during bulk writes.
- Legacy `preset` / `stage-index` keys in old exports are ignored.
- After a successful import: `StageConfig::refreshCache()` + `DevConfig::refreshCache()`, overlay / trajectory sync, then **`InLevelSettingsFilterKeeper::reloadAfterImport`** — if ModSettings is open, store values are pushed into SettingNode drafts and the popup rebuilds next frame (keybinds + Apply cleared; active tab restored). Success alert shows after that sync. Closed popup → next open already reads the store.
- PlanGold: `ConfigBundleKeys`, `ConfigBundleCodec`, and `ConfigBundle::selfCheck` (temp-file round-trip + applying-guard).

## Settings table

Geode setting titles follow tabs: **Main** (incl. Window) → **Scope** (+ Off Ranges) → **Assist** (Algorithm / Smart / Learning / Trigger World) → **Bake** → **Modes** → **Visual** → **Keys** → **Dev**. Preset packs are removed — windows live only on Main.

| Key | Type | Default | Min | Max | Group / notes |
|-----|------|---------|-----|-----|-------|
| enabled | bool | true | | | Main — master |
| safe-mode | bool | true | | | Main — ON: block saves when enabled or assist-touched; OFF: vanilla progress |
| assist-mode | string | Live | Live/Replay/Hybrid/BruteForce | | Main — same as Bake Cycle Assist Mode / Ctrl+Shift+M |
| window-unit | string | Ticks | Ticks/Frames | | Main / Window — Frames→×4 ticks @240; per-mode `*-ticks` use same unit |
| ticks-all | int | 4 | 0 | 48 | Main / Window |
| ticks-wave | int | 4 | 0 | 48 | Main / Window |
| debug-logging | bool | false | | | Main |
| perf-trace | bool | false | | | Main — section timings; spikes → console + `earlybuffer-perf.log` in mod save dir |
| lag-probe | bool | false | | | Main — death/reset/checkpoint episodes → `earlybuffer-lagprobe.log` |
| show-overlay | bool | false | | | Main |
| show-trajectory | bool | false | | | Visual |
| show-bake-path | bool | false | | | Visual — green BakeMap windows near player |
| show-pause-bake | bool | false | | | Main — Pause Status Lines (top-left) |
| show-pause-hub | bool | true | | | Main — EB button |
| save-feedback-sound | bool | false | | | Visual — per-tick MP3 cue on delayed inject |
| save-feedback-color | bool | false | | | Visual — flash player icon on delayed inject |
| save-feedback-counter | bool | false | | | Visual — world circle + +N at click (clears on death/restart); palette 1…8 `#ff614e`…`#c069fa`; cap 64 markers |
| save-icon-color | rgb | #7CFF4A | | | Visual — flash color when save-feedback-color is on |
| assist-only-practice | bool | false | | | Scope — gates all Assist Modes |
| assist-startpos-only | bool | false | | | Scope |
| assist-percent-min | int | 0 | 0 | 100 | Scope |
| assist-percent-max | int | 100 | 0 | 100 | Scope |
| assist-off-N-from/to | float | -1 | -1 | 100 | Scope — N=1..6; -1 unused; inclusive off range |
| death-slack | int | 2 | 0 | 16 | Assist / Algorithm — look-ahead slack, not zone |
| look-ahead-cap | int | 0 | 0 | 240 | Assist / Algorithm — 0=auto |
| assist-pick | string | Legacy | New/Legacy | | Assist / Algorithm — G1 pick (Legacy default) |
| bake-radius | int | 0 | 0 | 128 | Bake — 0=auto |
| cps-limit | int | 0 | 0 | 40 | Algorithm — 0=off |
| min-click-interval-ms | int | 0 | 0 | 500 | Algorithm — skip assist only |
| assist-orbs | bool | true | | | Algorithm |
| sim-budget-us | int | 1500 | 0 | 10000 | Algorithm — soft wall-clock; 0=unlimited. HUS replan uses `max(200, sim/2)`. **Mobile** clamp ≤2000 (0 stays 0) via `PlatformLimits` |
| tick-budget | int | 4000 | 500 | 20000 | Algorithm — hard tick sum; exhaust=Pass. Hybrid: verify+Live share one press envelope (verify ≤1200 first + Live reserve). HUS / Live arm verify keep `max(200, tick/4)`. **Mobile** (`PlatformLimits::isConstrainedDevice` = Android+iOS): clamp ≤2000 |
| assist-press | bool | true | | | Algorithm |
| assist-release | bool | true | | | Algorithm |
| smart-depth | int | 1 | 1 | 3 | Smart |
| smart-combos | int | 48 | 8 | 256 | Smart |
| smart-horizon | int | 0 | 0 | 320 | Smart — 0=auto |
| robust-k | int | 0 | 0 | 3 | Smart — 0=auto (mode/speed/dt var → 1–3) |
| robust-jitter | bool | false | | | Smart — re-sim top cand at firstDt ±3% |
| auto-tune | bool | false | | | Learning — never changes ticks; no trust-up from inject saves |
| self-bake | bool | false | | | Learning — record vanilla clicks in practice |
| self-bake-normal | bool | false | | | Learning — also record outside practice |
| self-bake-refine | bool | true | | | Learning — budgeted window search on flush (`enable-if` self-bake) |
| trigger-bake | bool | true | | | Trigger World — static mover cache for shadow sim |
| trigger-bake-skip-dynamic | bool | true | | | Trigger World — do not bake/probe follow-player etc. |
| assist-verify | string | off | off/ab | | Learning — debug A/B only |
| MODE-enable/overwrite/ticks/cap/slack/latency | mixed | see mod.json | | | Per-mode cube..swing; latencyWeight defaults Cube 0.25 … Wave 1.0 |
## Pseudocode plan()

1. tickBudget from opts (default 4000); if verifyAb then *2
2. modelFor; capture state
3. d0 press baseline -> ThreatZone clusterGapX=50
4. **no-threat Pass (G2)**: baseline did not die, zone invalid, no orb/pad hint → run `OpportunityProbe` (foresight already forces search; else cheap `d=mid` / `d=limit` sims). Pass only if probe empty; else full cand search. Budget starve with no probes → `PassBudget`.
4b. **wave-hazard-proximity Pass**: Wave + hazard AABB within ahead proximity (~70–140px) + no orb/pad → Pass even if a distant ThreatZone exists (avoids phase-shift “assists” in zigzag spam). **`foresightOrbPad` blocks this Pass** (keep searching for the orb). Runs after foresight/orb checks.
4c. After cand eval: **wave-futile-delay Pass** if Wave, no orbIntent, no foresight orb/pad, all surviveBin ≤0 and best endX gain &lt; 8 → Pass. Foresight orb/pad → search further. Skipped when `idleSafer`.
4d. **Continuous idleSafer / ascentUseful**: Ship/Wave/Swing/Ufo — if baseline idle survives better than d0 press (`idleSafer`) and some delay &gt;0 improves survival (`ascentUseful`), force Delay (New outcome / Legacy latest) instead of Pass/hopeless. No forever block if nothing in-window is viable; controller may Soft Hold when both flags set. Wave proximity/futile early-Pass skipped when `idleSafer`.
5. eval delays under tick + sim-budget-us
6. orbIntent: Legacy earliest ±k act; New outcome rank ±k; optional firstDt ±3% jitter
7. else d0 survived -> Pass; else New outcome / Legacy latest ±k-robust
8. New: `planSequence` exit-predict over survivor band (horizon replay; smartDepth≥2 adds multi-jump)
9. budgetHit -> PassBudget (controller may Soft Hold-Until-Safe); linked dual both clear

Lexico (New): orbUseful > surviveBin > surviveTicks > endX−λ·delay. Legacy non-orb: latest ±k-robust. Bins 24/48/96. k from `robust-k` or auto.

## Click intent (spam) v2

`ClickIntentGuard` ring of recent Jump presses per player (P1/P2 independent).

Pass when **density** (wave ≈ 3 / 10 ticks) **and** (**rhythmic** gaps: mean ≤ mode max, low σ, no mid-spam pause) **or** **very dense** (need+2). **Never Pass** for empty ring, a solitary click, or when max gap ≫ local mean (absolute pause vs mode threshold **or** relative outlier). First 1–2 clicks and pause-then-resume → assist allowed.

**Do not Pass** (allow assist) when spatial orb/pad ahead **or** a BakeMap window is nearby. Portal/teleport corridor spam looks rhythmic → Pass unless orb/pad. Gate Pass sets overlay `PASS spam` / `VERDICT pass-spam`.

Continuous modes also Pass release assist during spam. Reset on attempt / quit. Self-check: `ClickIntentGuard::selfCheck`.

## GhostParity / SpotTrust (hard Pass)

`GhostParityProbe` records live pose each physics tick, anchors every 16t into reserved shadow slots 14/15 (`PARITY_SLOT_A/B`, outside the plan capture ring 1..13), and every 16t replays the past ≤48t path via `samplePath`. Drift (ε **LOCKED** 12px via `AssistContracts140::kLockedEps*` / `applyLockedEps` at boot, or death mismatch) → `SimTrust::noteParityDrift` + `SpotTrustMap::markUntrusted(x)` + LagProbe `eb.parity.drift` + `AssistRealMetrics::noteParityProbe`. AssistController: mid-buffer drift clears the buffer (`pass-parity-drift`); `planPress`/`planRelease` hard-Pass on parity drift or spot-untrusted — **never** softened by orb foresight. Overlay: `trust=1` / `trust=0 parity` / `trust=spot`. PlanGold: `spot-trust-map`, `sim-trust`, `ghost-parity-probe`.

### Assist contracts v1.4.0 LOCK (Commit 2)

Frozen numbers live in `docs/assist_contracts_140.md` and `AssistContracts140` — **DO NOT RETUNE** without a new mod version. Real counters: `AssistRealMetrics` (`N_opp` / `N_trust_opp` / `A` / `DAF` / ParityOK + Wilson G5 / G6 / G7a / G7b TrustRate). Overlay bit `daf=a/A par=ok/n`. `dev-freeze-world` default **on**. Prev/Caused stay `simEstimate` only (G10). PlanGold: `assist-contracts-140`, `assist-real-metrics`.

## Divergence / UNTRUST

`DivergenceTracker` ring (48): bad if delayed assist died, or orb expected but missed (death or delayed-for-orb). Warn ≥35%/n≥8; UNTRUST ≥45%/n≥12. `pollOutcome` uses real `planDelay`; `onRealDeath` notes pending outcomes. Live/Hybrid-live: `simUntrusted` → hard Pass + overlay `UNTRUST pass` / `PASS untrust` **unless** TravelForesight orb/pad ahead — then soft (allow Delay / Soft Hold). Low-trust spot Pass uses the same orb override. `shouldWarn` soft: `capMul *= 0.85`. `clear()` on attempt reset (and PlanGold `DivergenceTracker::selfCheck`). Parity/SpotTrust gates above are separate and always hard.

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

**ArmedPress / Delay press buffer (pre-inject):** Early user UP while waiting for `fireTick` sets `releaseSeen` and **swallows** the UP — the buffer stays `Buffered` and inject is **still due**. Early release does **not** cancel Delay. Arm copies `requireHold` / `releaseAfter|Earliest|Latest` onto the buffer.

On inject (`resolveQueuedHoldTicks`):
- User still holding + `requireHold` → arm extend-after-release (never force-release while held).
- User already up (`releaseSeen`) + plan hold (`requireHold` or `releaseAfter > 0`) → **queued hold**: keep Jump down in GD for N ticks (continuous = earliest-safe only; clamp to window / `releaseLatest`), then inject UP.
- User already up + plain Delay (no hold plan) → ASAP release.
- Hard rule: while `userHolding`, never inject UP.

**Re-click** while ArmedPress + `releaseSeen` → fire A first, then plan B. **Re-click** while pending → cancel scheduled release and re-plan. Dash `requireHold` keeps release deferred while `m_isDashing` after a held inject. Helpers: `AssistController::resolveQueuedHoldTicks` / `queuedHoldSelfCheck`.

Branch releases `{2,5,10,18}` and dash-hold `40` seed the window when they already won viability.

## fireTick vs jumpTick

When arming a Delay press, `planDelay = plan.delay` and `expectedJumpTick = effectTick + jumpTick` (sim-relative).

`resolveFireTick(effectTick, delay, jumpTick, requireOrb)`:

- Non-orb and `jumpTick > delay` → `fireTick = effectTick + jumpTick` (inject when the jump matters, not one frame early).
- Orb / `jumpTick <= delay` → `fireTick = effectTick + delay` (activation window wins; hold covers lag).
- Always clamp to `pressTick + limit`.
- **`dMin`/`dMax` clamp only when `requireOrb`** — incidental activation bands from other cands must not collapse a threat Delay (Legacy and New). Otherwise `planDelay=+4` while `fireTick` snapped to an early orb cand → jump almost now.

`shouldFireBuffered`: fire when `attemptTick >= fireTick`, or on the last tick of the window if `planDelay` is already met (late-fire guard). SaveFeedback `+N` is **physics ticks** held back (`attemptTick - pressTick` at inject), not 60fps display frames (~4 ticks ≈ 1 frame @240tps).

Helpers: `AssistController::resolveFireTick` / `shouldFireBuffered` / `fireTickSelfCheck`.

## Recheck slide safety

Before inject, live recheck may slide `fireTick`. Rules (`acceptRecheckSlide`):

- Plan must be Delay, `delay > 0`, `!budgetHit`, and (`threatCleared` or orb).
- Cap late slides to `oldFire + 2` (non-orb); orb may wait for `dMin`.
- Never past `absMax - robustK` (collapsed margin falls back to `absMax`).
- Clamp to `pressTick + limit`. **Non-orb: reject early slides** (must not undo the planned wait).
- Log `recheck: oldFire→newFire` via AssistLog + PerfTrace.
- CPS / mode-change force-fire: `allowRecheck=false` (inject immediately).

## Pseudocode fireBuffered()

Wait for dMin if requireOrb; up to 2 safe reclamps into [dMin,dMax] (recheck also re-aligns jumpTick); inject; if `releaseSeen` + queued hold → PendingRelease for N ticks; else if `releaseSeen` → ASAP; else if `requireHold` and still holding → arm extend-after-release; else plain tap (user owns hold).
Press B while ArmedPress + releaseSeen: fire A first, then plan B.

## Buffer states

Idle | ArmedPress | ArmedRelease | PendingRelease

`BufferEventMachine::selfCheck` documents: early UP → `releaseSeen`, stay ArmedPress, fire still due; cancel drops buffer; force-fire still injects.

Cancel: death, resetLevel, quit, checkpoint load. Force-fire: mode-change, CPS unlock.

## Smart bake-chain

When `smartDepth > 1` and BakeMap has later clicks: `planSequence` scores each allowed `d0` with a fixed follow-up timeline from the next 1–2 bake/self-bake clicks (same mode; beyond horizon / mode change aborts). Score = survive + reach next X − weak latency. Only `d0` is buffered. No bake follow-ups → prior hypothetical d1 grid.

## Live bake-prior (prefer band)

`PlanOptions.preferDelayMin/Max` from a nearby BakeMap window (delay ticks). Cand order: d0 → **orb window** → prefer±k → rest. Early-exit after prefer margin if a ±k-robust prefer delay is viable (`!budgetHit`) — **suppressed** when orb foresight / orb-window is active so activations outside the prefer band are still evaluated. Final pick: orb activation path before prefer/lexico. Empty / out-of-limit band ignored.

## Adaptive robustK + latency (+ wave spam)

`robust-k` 0 → auto (`ShadowSimulator::autoRobustK`): Wave/Ship/Swing start at 2; speed >1.1 → ≥2, >1.5 → 3; relative dt EMA >0.08 bumps +1 (cap 3). **Wave + spam-dense** (ClickIntentGuard density, even when orb forces assist): keep ≥2; if speed >1.05 → **3**. Fixed 1–3 overrides auto. `robust-jitter`: after pick, re-run with firstDt×0.97/1.03; fail → mark non-viable and re-pick.

`ModeParams.latencyWeight` still softens Wave spam-dense assists (λ ×0.65). New miss-assist pick uses **outcome / exit prediction** (surviveTicks / endX−λ·delay / horizon replay) instead of fixed mid or latest.

**Starter defaults use ticks-all/wave = 4** so ±k-robust has room in spam. Set tighter/wider windows on Main → Window (no Preset packs). With auto k=2..3, a 2-tick window often cannot fit full ±k neighbors — `pickRobust` falls back to earliest viable.

## LevelProfile JSON

key (LevelContentKey fullKey with content rev), modes[Mode].capMul, modes[Mode].spots[bucket].{bias,trust,n,saved,diedAfterAssist,pessimism,histN,earlyHist[17]}

Earliness histogram bins −8…+8 from Pass/postmortem only (never inject saves). Laplace modal → delay offset when histN≥3; else classic bias±2. Bin decay on autosave.

**Histogram ±1 explore gate** (`AssistLearning::allowHistogramExplore`): while `histN < 8`, a ~20% roll may add ±1 to delay — **blocked** for Wave/Ship/Swing, when `ClickIntentGuard` density would Pass, when a bake-prefer band is active, or when `diedAfterAssist > 0` at that spot. Preferred delay offset (non-random) still applies.

## BakeMap JSON

levelKey, rev, tps (runtime-measured for brute), method, sourceReplay, clicks[{nominalFrame,earliest,latest,down,player2,x,mode}]

Bake runs serial on the main thread via BakeScheduler (no worker pool).

## Reentrancy

While simulating: no sounds/achievements/percentage/particles/trigger side-effects; SimCollisionPolicy filter; orb hold-gate.
Safe Mode ON + (enabled or assistTouchedRun) blocks percentage/newbest/test-complete. Safe Mode OFF never blocks progress.

Owned ghosts (shadow + snapshots) never enter vanilla `destroyPlayer` / `playerDestroyed` (incl. anticheatSpike).
Death to an owned ghost as the killer object is swallowed. Real-player deaths are swallowed while
`m_simulating` (anticheat on real players still passes). During sim, `collisionCheckObjects` runs
only for the shadow (filter via a **persistent** scratch `gd::vector`, never a stack temporary — iOS ABI).
Live collision lists strip owned ghosts. `SimSessionGuard` RAII always `restoreCapture` + clears
`m_simulating` / `m_applyingRing` and parks owned ghosts (shadow + eval off-level; snapshots scrub+hide)
after each plan/bake/traj session so live `playerTouchedRing` never sees half-restored claim flags.
Ring restore uses `typeinfo_cast<RingObject*>`; `m_activatedObjectIDs` is always restored.
Ghost `update` is no-op unless actively stepping. Before `resetLevel` / checkpoint load:
`evacuateGhosts` pulls all owned ghosts out of section vectors (avoids `sortSectionVector` AV)
and cancels an in-flight bake.

### Eclipse / multi-mod coexistence (iOS ring crash)

An iOS SIGBUS inside vanilla `playerTouchedRing` on the **live** path was observed with Eclipse’s
`PauseBGLHook` on `update` in the same stack. EarlyBuffer is a **plausible contributor** (nested
shadow sims mutate shared ring claim / activated IDs / section vectors on the same main-thread
update), not a proven sole cause — **do not “fix Eclipse.”** Isolation goals:

| Check | Expectation |
| --- | --- |
| EB off | Crash gone → EB involved; keep isolation |
| Assist off (EB loaded) | Same |
| Trajectory / Bake Visualizer off | Rules out traj nested sims |
| Eclipse off, EB on | If still crashes → EB-only; if gone → multi-mod interaction |

HUS replan refuses to nest a full `plan()` while `PlayerObject::update` is in progress
(`hus-defer-mid-update`). Prefer A/B the matrix above before filing cross-mod blame.

## Constants

clusterGapX=50, windowMarginK=1, tickBudget=4000, sim-budget-us=1500. HUS replan every 3 ticks / ΔX≥8 / foresight edge / rem≤2; caps `husReplanBudgetUs` / `husReplanTickBudget`. Platformer: assist off.

## Miss-assist contracts

Physics unit = **tick** (often ~240/s). UI “frames” at 60fps are **not** the same: 10 display frames ≈ 40 ticks at 240 TPS. Settings `ticks-*` are tick windows (Main → Window). Beginner defaults: Pick **Legacy**, windows **4/4**, Assist **Live**, Safe Mode **on**, Auto-Tune / Self-Bake **off**.

### Guarantees

| Id | Rule |
|----|------|
| G1 Window-Cap | `delay`, `fireTick`, bake/hold ETA never exceed `activeLimit` (`params.ticks`). Ideal timing past the window does not exist for assist — pick best among `0..limit` only. Example: ideal `d=6`, limit `4`, `d=4` viable → Delay ≤4, never wait for 6. |
| G1 Legacy | Setting `assist-pick` = **Legacy** (default): `d0` viable → Pass; else **latest** ±k-robust viable in window (non-orb). Orb band still earliest activation. |
| G1 Prefer (New) | `assist-pick` = **New**: **Delay-viable = sim survives**. d0 survived → Pass; else **exit prediction** ranks survivor delays by horizon outcome (survive / endX−λ·delay / bake-chain). Always runs `planSequence` exit probe. Live `verifyDelay` before arm; refuse lethal Delay → Pass. |
| G2 | `Pass no-threat` only after foresight + `OpportunityProbe` (mid/limit) agree the corridor is empty. Foresight orb/pad or probe activation/bin/endX gain → full search. |
| G3 | Every Pass/Delay sets `AssistReason`; overlay / `VERDICT` / `PLAN … verdict=` logs it. `pass-unknown` is a bug. |
| Soft Hold-Until-Safe | `PassBudget`, or `budgetHit`/`unknown` with foresight/orb opportunity, or continuous `idleSafer`+`ascentUseful` → arm buffer (`holdUntilSafe`). Cadenced replan (every 3 ticks / ΔX≥8 / foresight edge / rem≤2) with reduced caps — never hang: budget exhaust parks at deadline. Delay confirm updates `fireTick`; opportunity clear + safe → inject now; deadline → fire. Overlay `HUS` + `HOLD~`. |
| No lethal assist | Learning never pulls earlier than the plan; recheck slides ≤+2 later (never earlier); only surviving Delays arm; verify-fail → Pass (vanilla). |
| Continuous ascent | Ship/Wave: idle safer than d0 press + later delay helps → Delay/HUS, not Pass. Cube overhead not in this commit (narrow continuous-only). |
| Bake / Hybrid | Hybrid uses bake only after verify ok; **never soft-trust `budgetHit`**. Verify-fail / past-latest / missing → Live under the **shared press budget** (reserve + remainder; hitch caps after reject). Replay/Brute stamp `pass-bake-*`. Prefer-band early-exit suppressed under orb foresight (`preferBandEarlyExitAllowed`); final pick Orb before Prefer. |

Executable pure fixtures: `MissAssistContracts::selfCheck` (incl. prefer early-exit), `TravelForesight::selfCheck`, `AssistController::holdUntilSafeSelfCheck` (via PlanGold). Self-checks run on mod load (`PlanGoldCorpus`).

### Pass / Delay reasons (G3)

Stable `AssistVerdict::name` tokens (overlay bit in parentheses). `pass-unknown` must not ship.

| Token | Typical cause |
|-------|----------------|
| `pass-no-threat` | Empty corridor after foresight + OpportunityProbe |
| `pass-wave-proximity` / `pass-wave-futile` | Wave spam / no useful delay (blocked by foresight orb) |
| `pass-d0-safe` | Delay 0 already clears (Legacy preference / New best=0) |
| `pass-hopeless` / `pass-budget` | Nothing viable / tick wall; may arm HUS if foresight/ascent |
| `pass-toggle-unknown` | Toggle-orb / touch-toggle target overlapped in sim — refuse Delay (honesty; ToggleOrbSim accuracy is separate) |
| `pass-parity-drift` | GhostParityProbe: shadow replay of recent path drifted vs live (attempt-wide hard Pass; clears buffer) |
| `pass-spot-untrusted` | SpotTrustMap: X marked after prior parity drift (hard Pass until good-parity streak clears) |
| `pass-spam` / `pass-untrust` / `pass-low-trust` | Intent / divergence / LevelProfile trust gates (`pass-untrust` softens near orb; parity/spot never soften) |
| `pass-limit-zero` / `pass-debounce` / `pass-cps` / `pass-scope` / `pass-assist-off` / `pass-early-attempt` | Hard off / debounce / CPS / scope |
| `pass-bake-missing` / `pass-bake-no-window` / `pass-bake-past-latest` / `pass-bake-verify-fail` / `pass-bake-in-window` | Replay/Brute bake paths (Hybrid falls through to Live) |
| `delay-orb` / `delay-threat` / `delay-prefer` / `delay-generic` | Buffered Delay |
| `hold-pending` | Soft Hold-Until-Safe (`HOLD~` / `HUS`) |

### TravelForesight

`TravelForesight` replaces the old fixed `(limit+8)*5.5` ahead-X for orb hints. Ahead distance = clamp(`speedMul * (5.5/(1/240)) * (limit+slack)*simDt + 16`, 60…320). Scans **orbs and jump pads**; skips activated / claimed. `ShadowSimulator::spatialOrbHint` and controller spam overrides use the same cone. `PLAN` / `logPlan` emit `foresight=0|1` from `SimPlan.foresightOrbPad`.

### OpportunityProbe (G2)

Before blind `Pass no-threat`, `OpportunityProbe` forces further search when foresight/orb-window/d0-ring hits, or when a cheap sim at `mid=limit/2` or `limit` shows ring activation, higher surviveBin, or endX gain ≥ 8. Orb coast scan length = `limit+slack+4`; if empty, a budget-capped **press-to-reach** scan holds Jump. Fixtures: `OpportunityProbe::selfCheck`.

### Manual playtest matrix

| Scene | Expect (New) | Expect (Legacy) | Notes |
|-------|--------------|-----------------|-------|
| Cube orb, limit 16, press ~10 ticks early | Delay/HOLD/HUS, fire in orb window, SaveFeedback `+N` | Pass if d0 clears without orb-activation path; Delay if orb window forces wait | Debug: `VERDICT delay-*` / `hold-pending` or `PASS d0-safe` |
| Budget starve mid-search + foresight orb | `HUS` → cadenced replan → Delay or deadline fire | Same soft-hold if PassBudget / budget+foresight | Never silent Pass / never hang; LagProbe `eb.hus.replan` |
| Cube spike jump early, d0 would die | Delay toward later ±k-robust ≤limit | Same (latest ±k-robust) | Not earliest-viable edge |
| Toggle-orb corridor (solids flip on 1594) | Delay only after modeled flip; else `PASS toggle-unk` / HUS | Same | `ToggleOrbSim` restores on capture teardown |
| Empty corridor, single tap | `PASS no-threat`, vanilla now | Same | |
| Wave zigzag spam, no orb | Pass spam / wave-prox — no Delay | Same | Overlay `PASS spam` / `PASS wave-prox` |
| Wave + orb in travel cone | Delay/Hold — wave-prox/futile blocked by foresight | Same | `foresightOrbPad` overrides wave Pass |
| Ship/Wave: idle lives, press-now dies, later delay clears | Delay/HUS (`idleSafer`+`ascent`) | Same | PLAN log `idleSafer=1 ascent=1` |
| Hybrid: bake hit (verify ok, in window) | Overlay `hy bake [lo,hi]`; LagProbe `eb.hybrid.verify` short / `cache=1` on repeat | Same | No Live plan this press |
| Hybrid: bake miss (no window / missing) | Live plan; overlay map status or `hy … → live`; `eb.hybrid.live` only | Same | No verify scope |
| Hybrid: bake verify fails near orb | Live foresight plan (Delay/HUS), overlay `hy verify … → live` | Same | No `pass-bake-verify-fail`; LagProbe `eb.hybrid.verify` then `eb.hybrid.live` |
| Hybrid: constrained-device starve | Verify may `budgetHit` → Live with reserve/remainder; PassBudget/HUS if foresight | Same | Never soft-trust bake; LagProbe remUs/remTick in `eb.hybrid.live` detail |
| Replay: past bake latest | Pass `pass-bake-past-latest` | Same | Bake-only |
| Prefer band early + orb outside band | Keep searching; orb activation pick | Same | No prefer early-exit |
| Single early click, empty ring | Reach plan (not spam Pass) | Same | |
| UNTRUST + orb foresight | Soft: Delay/HUS allowed | Same | Hard UNTRUST Pass only without orb |
| Low-trust spot + orb foresight | Soft: plan allowed | Same | Orb overrides trust Pass |
| Dash orb | `requireHold`, no abort while held | Same | |
| Ideal d=6, limit=4, d=4 survives | Delay ≤4 | Delay ≤4 if d0 unsafe; Pass if d0 safe | Never fire at 6 |
| “10 frames” early @60fps into ticks=16 | Usually Pass (outside window) | Same | Remap frames→ticks before blaming assist |
| MH speedhack ≠1 | Delays in sim ticks via AssistClock | Same | |
| Dual linked | Both must clear | Same | |
| Death / respawn | Buffer clear, assist re-attaches | Same | |

Debug: enable **Debug Logging**; look for `PLAN id=… verdict=… delay=… orbIntent=… foresight=…` and `VERDICT …`. No `ASSIST press +N` means the press never armed a buffer.

## Authorship

Design and review: maintainer (Audesign). Drafting used AI tools; every shipped line is human-reviewed and actively tested (see repository README).
