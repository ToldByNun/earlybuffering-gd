# Changelog

## v1.5.1-dev

### AP4 — Orbs under input-window
- Live IW runs cheap `spatialOrbHint` whenever Orbs are on, plus budget-capped `scanOrbWindow` (coast + press-reach).
- After cand fill: orb-intent → earliest±k activation band as `DelayOrb` / `requireOrb` / `dMin`–`dMax` candNotes (before Wave help-only).
- Dash hold salvage (`releaseAfter=40`) and toggle → `PassToggleUnknown` on the IW path.
- Adaptive Gate Delay with activation sets `requireOrb` + DelayOrb honestly.
- PlanGold: `orb-under-input-window`, `orb-dash-hold-iw`, `pass-toggle-unknown-iw`.

### AP3b — Live IW Wave anti-phase + help-only
- Continuous Wave IW no longer skips classic anti-phase: **help-only** (d0 Alive → `PassD0Safe`, never Delay to pad endX), **proximity** (`PassWaveProximity`), **futile** (`PassWaveFutile`), earliest later Alive Delay when d0 dies.
- Cheap baseline coast under continuous IW for `idleSafer`. PlanGold: `wave-iw-help-only`, `wave-iw-prox-pass`.

### AP3a — Wave horizon floor
- Continuous Live IW / Adaptive no longer overwrite look-ahead with bare Max Assist limit.
- Cap = `max(continuousLookAhead, short AssistHorizon planCap)`; Wave short floor **32** ticks (`EbLivePlanContracts::kWaveHorizonFloor`).
- `kPostCapProveTicks` still appended after the floor. PlanGold: `wave-horizon-floor`. Wave regression retargeted + legacy short-horizon dual assert.

### AP1 — Hitch (small levels)
- Live `sim-budget-us=0` → **8000µs** hitch ceiling for classic **and** Adaptive (`EbLivePlanContracts::intendedLiveBudgetUs`).
- Adaptive→classic fallthrough uses remUs floor **2000µs** when the wall is spent but ticks remain (was `budgetUs=1`).
- Fallthrough seeds observation-cleared Alive screens (not only terminal/horizon); IW accepts those seeds at screen quantum.
- Discrete peak-death cut marks leftovers `Skipped`/`peak-death-cut` only — no fake-dead clones; IW does not re-`evalDelay` them.
- PlanGold: `live-hitch-ceiling`. Adaptive parity smoke: `adaptive-zero-wall-clamped`, `fallthrough-remus-floor`.

### AP0 — Live plan contracts (no behavior change)
- Ledger: `peak-death-cut` reject code + `EarlyExit::PeakDeath`.
- Pure `EbLivePlanContracts` locks current vs intended hitch / landed-rank / peak-death / orb-under-IW tables. PlanGold: `live-plan-contracts`.
- DevTrace `planFinished` adds `planUs`, `budgetHit`, `candidatesTried`, `adaptiveShell`, `adaptiveFellThrough`.

### Idle-prefix sharing (classic ascending delays)
- Classic `plan()` evaluates delays 1..limit on one shared idle spine: checkpoint player+world at each pre-press, fork the press path, restore, then advance one idle tick.
- Same trajectories as independent `runOne(d)`; idle physics drops from ~N(N+1)/2 to ~N (large Max Assist).
- Delay 0 / traj engine / dual reset still use the prior path. PlanGold: `idle-prefix-share`.
- RequireAlive **input-window** (cube/ball) now fills 1..limit via the same idle spine before ranking (was still per-delay `evalDelay`).

### Grief ball input-window ranking
- `stopAtBallLanding` made `ticksRan ≈ delay + flight`, so later delays faked higher `surviveBin` and Live armed late peaks (session: Live d=7, Eval `bestSurv=0`).
- Landed observation-safe cands now rank by endX−λ·delay only, and need >`kFutileEndXGain` to beat an earlier Delay.

### Live click hitch (`sim-budget-us=0`)
- Classic Live no longer treats `sim-budget-us=0` as a multi-frame freeze: soft wall clamps to **8000µs** per press.
- **AP1:** Adaptive Live uses the same hitch ceiling; peak-death leftovers stay honest Skipped (see AP1 above).

### Dynamic / Adaptive throughput (no budget cuts)
- Phase C verify rebuilds past Adaptive screen depth on the first cold grant, then keeps the checkpoint for resume-extend (no more short 0..N re-runs that never extend).
- Adaptive→classic fallthrough seeds terminal / horizon-proven / observation-cleared Alive screens + threat zone + prefix-death tick so classic skips re-sim of finished Adaptive work.

### Dev Lab resource stats
- Session JSON + Dev report overlay now carry process RAM/CPU min–max (`resourceStats` / `ram=…-…MB cpu=…-…%`), sampled on session start, planFinished, and attempt seal (Windows Working Set + process CPU).

### Live honesty (Dump-SP / startpos class)
- Touch movers and follow-like static targets that TW cannot bake are registered as Hard Unknown (no more frozen editor-pose “safe” Delays).
- Live Delay proof adds post-horizon prove-out ticks so a death on the frame after hitCap is not armed as Safe.

### Ledger honesty + tiny Max Assist windows
- CandNotes no longer label same-crash / continuous earliest-safe cuts as fake `skip=not-viable` — codes are `same-crash-cut` and `earliest-safe-exit`.
- Discrete input-window (cube/ball) ranks the full delay band by survive_then_endx; continuous still earliest-safe.
- `robustK` is clamped to the Max Assist window (`limit=1` → k=0) so a 1-tick cube Delay no longer needs a viable d0 neighbor to arm.

### Per-tick shell cheaper (same outcomes)
- Capture builds a group→members sidetable once (`belongsToGroup` per solid×needed group, not per effect×solid) for follow / toggle / unmodeled targets.
- TW / live-dynamic apply skips `writePose` when the sampled pose matches the last write (hold keys no longer rewrite Cocos every tick).
- Ghost mute runs once per session / `copyPlayerState`, not 3× inside each physics tick.

### Shadow ghosts vs hitbox-on-death menus
- Owned ghosts no longer leave `m_isDead` latched after sim collisions; death stays on `simulator.m_dead` only.
- `PlayLayer::destroyPlayer` uses `Priority::Stub` swallow; `collidedWithObjectInternal` clears ghost deaths too.
- Sim sessions force `m_hitboxesOnDeath=false` and hide `m_debugDrawNode`; restore after. Real P1/P2 death flags restored if sim contaminated them.
- After a ghost death swallow, `postUpdate` (Last) keeps `m_debugDrawNode` hidden while the real icon is alive — undoes Mega Hack / OpenHack `s_isDead` latches that fire on any `destroyPlayer` call.
- Suppress clears on real death / `resetLevel`. Ghosts tagged `audesign.earlybuffer/sim-ghost` (+ node ids) for cooperative ignore.

## v1.5.0

**Adaptive Prediction Planner (Live) + assist honesty** (still GD **2.2081** only). Multi-platform package: Windows + macOS + iOS + Android32/64.

### Adaptive Prediction Planner
- Live presses can use Adaptive Plan Search (`adaptive-plan`): short screen → verify quanta → DecisionGate.
- Three hard limits stay separate: Max Assist ticks, Prediction Horizon, Dynamic Plan Tick Budget (`tick-budget` / `sim-budget-us`).
- Soft wall stays a runtime ceiling (does not shrink hard ticks via EMA).
- Gate arms Delay only for Autonomous Alive + hazard progress (horizon / zone / past dying d0); screen-Alive alone is not `PassD0Safe`.
- Unproven / Hopeless Adaptive Pass falls through to classic so Assist is not starved under tight or unlimited soft wall.
- Default `sim-budget-us` **4000**; Adaptive Live floor raises lower positive budgets to 4000.
- PlanGold + offline Adaptive parity smoke lock fallthrough / Gate regressions.

### Packaging / CI
- Clang nested-`Context` default-arg fix in Assist Pick v2 (Android / iOS / Windows / macOS CI).

### Docs
- Release notes: [`releases/v1.5.0.md`](releases/v1.5.0.md); Adaptive docs in `docs/assist_reference.md`.

## v1.4.3

- Adds Cloud Configs below Import Config in Settings → Assist → Assist Actions.
- Catalog search, sorting, installation, sharing, download counts and star ratings.
- Preserves keybinds and developer settings during cloud installation.
- Includes cached packs and bundled offline defaults.
- Retains the working assist implementation from the current development baseline.
- Updates release, installation and network/privacy documentation.
- Local Windows build uses `geode build`. Other platform packages require their platform builds and checks.


## v1.3.2

**Assist honesty + bake fidelity + Main Window IA + iOS harden** (still GD **2.2081** only). Multi-platform package: Windows + macOS + iOS + Android32/64.

### Queued tap / hold (Problem A)
- Early UP while a Delay is buffered no longer cancels the buffer; plain Delay still fires then ASAP-releases.
- Plans with `requireHold` / `releaseAfter` keep the planned hold after inject when the user already released (queued hold), without force-releasing while Jump is still held.
- Docs: press-queue vs post-fire hold-extend clarified; PlanGold `queuedHoldSelfCheck`.

### Toggle orbs / “dying to nothing” (Problem B)
- Toggle-orb / touch-toggle targets marked **UnknownOnOverlap**; Delay refused when the path hits unknown toggle geometry (`PassToggleUnknown`).
- **`ToggleOrbSim`**: reversible group flip on shadow hit (1594 / touch-1049) without real `triggerObject`; restore with capture teardown.
- Inactive StaticToggle bake hardens collision (not visibility-only).

### Trigger World bake (Problem D)
- Soft ceiling ≥ **100 000** tracks (loud `tw softcap`); no silent 2048 truncate of bakeable static movers.
- Collect finishes every object-layer child across slices; section scan for extras; long movers decimate keys under softcap.

### Replay / Brute bake (Problem E)
- Replay + BruteForce bake produce usable windows again (start snapshot, author TPS 1:1 for Physics Bypass, press+release same-frame, P2 handling).
- Empty bake (`clicks=0`) no longer looks “loaded”; clear status / alerts.
- Ghost hitbox fidelity: `copyPlayerState` refresh + clear stale jump hold; mode art / `switchedToMode` for non-cube ghosts.

### Bake Visualizer (Problem F)
- Visual tab **Bake Visualizer** (`show-bake-path`): green BakeMap windows near the player (off by default).

### Main tab IA — presets removed (Problem G)
- **Presets gone:** no `preset` dropdown, Cycle Preset action, F6 / `hotkey-cycle`, or `stage-index` Internal mirror.
- Tick windows only on **Main → Window** (`window-unit` / `ticks-all` / `ticks-wave`).
- Assist Off = **Enabled** off. Config import ignores legacy `preset` / `stage-index`.
- Portable `ConfigBundle` keys **124** (was 127 with preset/stage/hotkey-cycle).

### iOS / shared collision hardening (Problem C)
- **`SimSessionGuard` RAII** around plan / verify / bake / traj / replay — always restoreCapture, clear flags, park ghosts.
- Ring restore via `typeinfo_cast`; always restore `m_activatedObjectIDs`; `applyRingJump` RAII for `m_applyingRing`.
- **`collisionCheckObjects`** uses a persistent filter buffer (no stack `gd::vector`); null-reject live `playerTouchedRing`.
- Park/scrub all owned ghosts after sim; HUS defers mid-`PlayerObject::update` replan; Eclipse coexistence notes in `docs/assist_reference.md`.
- **iOS load:** CMake POST_BUILD `ldid`/`codesign`; `geode_package_check.py --require ios` checks `LC_CODE_SIGNATURE`; CI iOS job gates; `INSTALL.md` covers `missing code signature` / `Unable to load the DYLIB`.

### Packaging / docs
- `INSTALL.md` iOS signature troubleshooting; CI verifies signed `.ios.dylib`.
- README / about / support / `assist_reference` → **v1.3.2**; ship notes: [`releases/v1.3.2.md`](releases/v1.3.2.md).

## v1.3.1


**Settings UI overhaul, config import sync, assist clock fix** (still GD **2.2081** only). Multi-platform package: Windows + macOS + iOS + Android32/64.

### Settings UI
- **8-tab chip strip** on Geode `ModSettingsPopup` (Main / Scope / Assist / Bake / Modes / Visual / Keys / Dev): text-only white/grey labels under search; filters rows by exhaustive key→tab map (no Assist catch-all).
- **Copy / titles:** Main page title, accurate setting names (`assist-only-practice`, Window Unit on per-mode ticks, Dev actions blurb, pause status naming, etc.).
- **Removed dead keys** (old JSON skips them; other keys still apply): `show-pause-assist`, `swallow-hopeless`, `dev-record-include-pass`, `dev-eval-budget-us`. Portable `ConfigBundle` count **126** (was 130).
- **Config import UI sync:** after import, refresh Stage/Dev caches; push store → open SettingNodes; rebuild popup next frame so green Apply cannot overwrite the import; restore active tab; success alert after sync.

### Assist / clock
- **Ghost physics timestep:** GD step `0.25` is normal (not a hitch); internal assist clock uses seconds, ghost I/O converts via `gdStepToSeconds` / `secondsToGdStep` — restores sane Live/Hybrid delays after the 1.3.0 hitch-poison path.
- **Dev instrumentation:** session `settingsFingerprint`; witness Trigger-World bake counters; DelayArm/Fire/Outcome verify/budget/holdExtend/`deathAfterFire`; LagProbe `eb.assist.halluc`; overlay `tw empty|N/M`; Dev Eval `liveVsEvalMismatch`.

### Packaging
- Android NDK: fix clang nested `DelayArmMeta` default-init so Android32/64 CI links again.
- Docs: `docs/assist_reference.md` Pause/settings + Config sections; ship notes under `releases/`.

## v1.3.0

**Config export, Hybrid lag, beginner defaults, iOS mobile parity** (still GD **2.2081** only).

### Config export / import (full settings)
- **ConfigBundle v2**: exports/imports all **130** portable settings under `{ version, settings }` (not the old ~12-key subset).
- Types: bool/int/float/string, color as `#RRGGBB`, keybinds as Geode strings (`F6`, `Ctrl+Shift+D`, multi via ` | `).
- Import: v1 flat-root still works; merge missing keys; skip bad types; **applying-guard** so preset does not wipe custom ticks / look-ahead.
- Alerts show version / applied|keys / skipped + path. PlanGold: `ConfigBundleKeys` / `ConfigBundleCodec` / `ConfigBundle` round-trip selfCheck.
- Docs: `docs/assist_reference.md` Config export/import section.

### Safe Mode progress gate
- **Safe Mode OFF** now allows percentage / new best / normal complete even when assist delayed a click. Previously `assistTouchedRun` (and enabled→test-mode) always blocked progress regardless of Safe Mode.
- **Safe Mode ON** (default): still blocks when assist is enabled or this attempt was assist-touched.
- `StageConfig::blocksProgressSaves` live-reads `safe-mode` / `enabled` at the gate (pause toggle mid-run cannot stick on a stale cache).

### Beginner defaults
- Preset **Normal**, windows **4/4**, Pick **Legacy**, Assist Mode **Live**, Safe Mode **on**.
- Visuals / debug off by default: overlay, pause bake status, save-feedback sound/color, debug logging, lag probe.
- **Auto-Tune Per Level** and **Self-Bake (Practice)** default **off** (less background CPU / no surprise learning). Existing Geode saves keep the user’s prior values.

### Hybrid lag (bake verify)
- `verifyDelay`: when planned delay is **0**, reuse the d0 zone-probe `runOne` (cuts a duplicate sim on in-window / earliest hits). Linked dual still runs once; budget/wall unchanged (`budgetHit` still refuses bake trust).
- **Shared press budget:** Hybrid bake verify + Live fallthrough share one wall/tick envelope (`PressSimBudget`). Verify spends first (≤1200 ticks); Live uses the remainder — no second full press plan after a starved verify. Arm verify + HUS caps unchanged. Replay/Brute Pass-on-fail unchanged.
- **Hitch fix (~0.75 feel / stutter near ~5 CPS):** after Hybrid bake verify rejects a window, Live no longer treats near-bake as spam override, does not reuse the rejected prefer-band, and is hard-capped to ≤½ press tick/wall (plus shared remainder). Tight wall remainder skips exit-predict. Vanilla clicks never blocked.
- **Shared-budget regression:** exhausted Hybrid `usLeft==0` no longer maps to PlanOptions “unlimited” wall (that made verify-fail→Live unbounded). Verify leaves a Live reserve (500µs / 800 ticks); remainder `0` forces a 1µs fast budget exit instead.
- **Reuse findNear + threatZone:** Hybrid fallthrough copies bake window fields (no vector pointers) and passes verify `threatZone` into Live `plan()` via `seedThreatZone` (skips `buildThreatZone`). Missing-bake path leaves reuse empty; cleared each press.
- **HybridVerifyCache:** attempt-scoped memo by window fields + delay (no bake-vector pointers); X drift >8px miss; `budgetHit` not stored. Cleared on reset/death.
- **Hybrid LagProbe + overlay:** standalone `HYBRID eb.hybrid.verify` / `eb.hybrid.live` lines (Lag Probe on, no death episode); overlay prefixes `hy bake […]` vs `hy … → live`. Playtest rows: bake hit / miss / verify-fail→Live / constrained starve.
- **PlatformLimits:** `isConstrainedDevice()` = **Android + iOS** (not macOS/Windows). Same ceilings as before (sim ≤2000µs, tick ≤2000, overlay top 28, no disk PlanGold on load). SelfCheck reports mobile vs desktop.
- **Mobile I/O alerts:** Config export/import, replay-drop, Dev session/report show the full `SaveDropPaths` path and **copy it to the clipboard** (best-effort; alert still shows the path if copy fails). Neutral Files / Share / file manager / Finder-via-device wording (no ADB-only instructions). `Mod::get()` null still falls back to `earlybuffer-save`.
- **iOS packaging docs:** `INSTALL.md` iOS build + smoke checklist; `geode_package_check.py --require ios` (binary `audesign.earlybuffer.ios.dylib`); merge/TEMPLATE examples; README notes iOS shares mobile I/O + constrained budgets with Android.
- **Docs sync:** `docs/assist_reference.md` shared-budget Hybrid policy (not soft-trust), `PlatformLimits` Android+iOS section, beginner defaults (Pick Legacy / Auto-Tune+Self-Bake off). `about.md` Pick/Hybrid/defaults + iOS package line aligned.

### Assist clock / first-click
- Fix hitch poisoning of `m_lastDt` (stuck `simDt≈0.25` → `simTps≈4`, `speedScaleSuspect`) that made opening clicks `pass-hopeless` / useless Delay on Legacy and New. Sane physics samples update EMA both ways; absurd quanta snap planning to nominal 240 TPS.
- Dev Lab sessions on Dump StartPos / Slaughterhouse showed this clock failure on first DOWN.

### Window unit
- Setting **Window Unit**: `Ticks` (default) or `Frames` (60fps). Numbers under Default Window / per-mode overwrite convert to physics ticks (1 frame ≈ 4 ticks @240). Planning always uses ticks.

## v1.2.2

**Android64 package + mobile I/O** (still GD **2.2081** only).

### Packaging
- Release `.geode` includes **`audesign.earlybuffer.android64.so`** (and Windows `.dll` when merged). Windows-only local builds fail on phone with Geode’s missing-binary error.
- Tools: `tools/geode_package_check.py`, `tools/merge_geode_packages.py`. Build/merge + device smoke: `INSTALL.md`. GitHub release notes staging: `releases/v1.2.2.md`.

### Mobile I/O
- Non-Windows **Import Replay** via save-dir `replay-drop/` (newest `.gdr` / `.gdr2` / `.mhr` / `.json` / `.slc`).
- Config Export/Import uses `earlybuffer-config.json` under the mod save dir; alerts show the full path.
- Dev **Copy Report** dual-writes clipboard + `earlybuffer-dev-report.txt`.
- Drop folders created on mod load when possible.

### Android runtime
- PlanGold skips disk DevTrace self-checks on load (in-memory checks remain).
- Planner sim/tick budgets capped on Android (≤ 2000 µs / ≤ 2000 ticks).
- Status overlay top inset increased for notched screens.

### Build
- CMake: `comdlg32` stays Win32-only; Android NDK compile fixes (UTF-8 sources, activated-object map key).

## v1.2.1

**Miss-assist / verdict pass for GD 2.2081** (same final game line as 1.2.0).

- **Pass reasons + overlay** — every Pass/Delay stamps `AssistReason` (`VERDICT` / `PLAN … verdict=`).
- **Pick Mode** (`assist-pick`): **New** (default, exit/outcome prediction) vs **Legacy** (d0 Pass / latest ±k-robust).
- **TravelForesight** + **OpportunityProbe** (G2 no-threat); Wave / spam / UNTRUST soft when orb/pad ahead.
- **Soft Hold-Until-Safe** with cadenced replan (budget caps; never hang past Limit).
- Continuous **idleSafer / ascentUseful** (Ship/Wave); Hybrid bake verify-fail → Live foresight.
- Tick ≠ frame documented; PlanGold self-checks on load.

## v1.2.0

**Final EarlyBuffer release for Geometry Dash 2.2081.** Newer GD builds need a separate port.

### Pause settings (EB)

- Pause stays clean: top-right **EB** opens Geode settings; optional status lines top-left (`show-pause-bake`).
- Side bake/assist button stacks removed — actions live in settings pages.
- Category switcher: **Main → Bake → Ingame → Assist → Visual → Modes → Keys**.
- **Bake** page: Cycle Mode / Import Replay / Bake Map / Brute Bake / Cancel + live status (poll while pause open).
- **Assist** page: Cycle Preset / Reset Learn / Export·Import Cfg above the assist settings rows.
- Re-open EB is a no-op while settings are already open; scroll preserved across Geode rebuilds.

### Save feedback toggles

- Replaced combined `save-feedback` (off/icon/sound/both) with three Visual toggles: **Sound**, **Color** (icon flash), **Counter** (world +N).
- Defaults match old `both` (sound+color on, counter off). Re-check toggles if you previously used off/sound/icon-only.
- **Counter**: hollow ring + `+N` in GD font at the assisted click, colored by frames saved (1–8 palette); stays until death/restart; max 64 markers.

### Trigger World Bake

- Settings: **Trigger World Bake** + **Skip Dynamic** (defaults on).
- Content-keyed cache (`triggers/*.json`): spawn-timed static Move/Rotate/Scale/Toggle keyframes for the shadow sim.
- Budgeted build on enter (`tickSlice`); apply + restore transforms during plan; ready tracks promote from Unknown-on-overlap to Include.
- Player-follow / toggle-orb stay dynamic (Unknown unless optional live probe with skip-dynamic off).
- Lifecycle: loadOrBuild on enter; keep on checkpoint; cancel on death (resume if empty); save on quit/complete.
- Apply perf: near-ghost-first + per-tick object/time caps (far movers hold last pose).
- Settings info texts rewritten for plain-language high/low tradeoffs.

### Perf / Lag Probe

- **Perf Trace** (default off): times assist hot paths; spikes → console + `earlybuffer-perf.log`.
- **Lag Probe** (default on): death/reset/checkpoint episode timings → `earlybuffer-lagprobe.log` (legend included).
- Save-cue console spam gated behind Debug Logging.

### Crash / hitch fixes

- Evacuate shadow+snapshot ghosts from section vectors before `resetLevel` / checkpoint load.
- Safer ghost scrub (no bare `removeObjectFromSection` on null slots); quit detach without extra retains.
- Death path stays lean (no refine / disk on destroyPlayer); self-bake merge/refine amortized on idle ticks.

### Adaptive robust window + latency cost

- Smart settings: **Robust Window (±k)** (`0` = auto from mode/speed/dt variance) and optional **firstDt jitter**.
- `betterCand` ranks by `endX − latencyWeight·delay` after surviveBin; per-mode defaults (Wave 1.0 … Cube 0.25).

### Live bake-prior / smart / learning

- Nearby BakeMap window becomes a Live prefer delay band; early-exit when a robust prefer delay is found.
- `planSequence` scores `d0` using next bake/self-bake follow-up clicks when available.
- Per-spot click-earliness histogram; Laplace modal; decay + gated explore (no Wave/spam ±1 explore when intent would Pass).

### Divergence / UNTRUST

- Outcome tracking into `DivergenceTracker`; Live/Hybrid-live `simUntrusted` → force Pass + overlay hint.
- Soft warn shortens planner `capMul`.

### Self-bake

- **Self-Bake (Practice)** on by default; optional normal-mode; optional refine.
- Vanilla clicks → BakeMap (`method=self`); assist injects never recorded.
- Death/reset merge-only (idle drain); checkpoint/complete/quit may refine when safe.

### Bake verify

- `findNear` prefers matching game mode, then falls back.
- Hybrid/Replay/Brute: one-shot live `verifyDelay` before using a bake window.

### Settings / docs

- Regrouped Geode settings; Trigger World group; Internal for buried keys.
- README / about / support / contributing updated for Index release; repo `earlybuffering-gd`.

## v1.1.0

### Fixes

- **Death-to-nothing:** owned shadow/snapshot players never enter vanilla `destroyPlayer` / `playerDestroyed` (including anticheat). Dying to a ghost body as the killer object is swallowed.
- While simulating, real-player collision is skipped; live collision lists strip owned ghosts; real deaths mid-sim are swallowed (anticheat on real players still passes).
- Active shadow is parked off-level after each sim with wave trail reset; ghost `update` is a no-op unless actively stepping.

### Save feedback

- **Save Feedback** setting: `off` / `icon` / `sound` / `both` (default `both`).
- Per-tick sound cues for +1…+7 delayed ticks; +8+ uses the 8-tick clip (shipped in the mod package under resources).
- Icon flash uses **Save Icon Color** (color picker).

### Project

- Mod logo (`logo.png`) for Geode / Index listing.

## v1.0.0

First public release as **EarlyBuffer** (formerly *frirecracker* / *EarlyGrace*).

### Assist

- Threat-zone Live planner with optional orb activation windows (earliest +/-k-robust; no midpoint)
- Modes: Live, Hybrid, Replay, BruteForce (honest bake windows)
- Buffer lifecycle: Press B fires A then plans B; checkpoint / death / reset clears buffer
- Platformer hard-disable; practice / startpos / percent allow window
- Six float **Assist Off** percent ranges (From/To, `-1` = unused)
- Safe Mode + assistTouchedRun progress gating
- Per-level auto-tune with decay / explore; no trust-up from inject saves
- Runtime TPS for brute bake; serial main-thread BakeScheduler (no Cocos worker threads)
- Sim collision policy (unknown on movers / mode portals); divergence + perf overlay bits
- Pause UI: bake controls, hotkey mirrors, config export/import
- Plan gold / shadow self-check on mod load

### Project

- Mod ID `audesign.earlybuffer`; Geode 5.11.1 / GD 2.2081
- Docs, CI multi-platform workflow, contributing guide
