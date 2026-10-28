# Changelog

## v1.2.0 (unreleased)

### Bake verify

- `findNear` prefers matching game mode, then falls back (legacy maps).
- Hybrid/Replay/Brute: one-shot live `verifyDelay` (clearsThreat) before using a bake window.
- Verify fail → Hybrid falls through to Live; Replay/Brute Pass.

### Settings

- Regrouped Geode settings: **General** → **Assist Scope** → **Default Window** → **Algorithm** → **Smart Prediction** → **Learning** → per-mode → **Keybinds** → **Internal**.
- Moved practice / startpos / percent / off-ranges under Assist Scope; `auto-tune` + `assist-verify` under Learning; plan budgets under Algorithm.
- Buried `stage-index` and deprecated `swallow-hopeless` under Internal.

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
