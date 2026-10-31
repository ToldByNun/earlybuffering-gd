# Changelog

## Unreleased

### Main tab IA (Problem G) — presets removed
- **Presets gone:** no `preset` dropdown, no Cycle Preset action, no F6 / `hotkey-cycle`, no `stage-index` / Internal mirror. Tick windows only via **Main → Window** (`window-unit` / `ticks-all` / `ticks-wave`).
- Assist Off = **Enabled** off (not a Preset pack). Config import ignores legacy `preset` / `stage-index` keys.
- Filter map / ConfigBundleKeys / docs synced (Main owns windows; Assist actions = Reset Learn + Export/Import).

### iOS / shared collision hardening (Problem C)
- **`SimSessionGuard` RAII** around plan / verify / bake / traj / replay sessions — always restoreCapture, clear `m_simulating` / `m_applyingRing`, park ghosts (even on early return).
- Ring restore via **`typeinfo_cast<RingObject*>`**; always restore **`m_activatedObjectIDs`**; `applyRingJump` clears applying-flag with RAII.
- **`collisionCheckObjects`** uses a persistent filter buffer (no stack `gd::vector`); null-reject live `playerTouchedRing` args.
- Park/scrub **all** owned ghosts after sim (snapshots scrub+hide only — pose kept for bake/traj).
- HUS: defer mid-`PlayerObject::update` replan (`hus-defer-mid-update`); Eclipse coexistence A/B matrix in `docs/assist_reference.md`.
- **iOS load:** CMake POST_BUILD `ldid`/`codesign` ad-hoc sign; `geode_package_check.py --require ios` verifies `LC_CODE_SIGNATURE`; CI iOS job gates on it; `INSTALL.md` covers `missing code signature` / `Unable to load the DYLIB`.

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
