# EarlyBuffer

> **Docs (players + developers):** https://ToldByNun.github.io/earlybuffering-gd/  
> Local preview: `npm install` → `npm run docs:dev`  
> Install, features, settings, problems, how it works, and the **Dev** section live there. **Not on the Geode Index** — install from [Releases](https://github.com/ToldByNun/earlybuffering-gd/releases).

**Click early. Still live.**

Geode timing assist for Geometry Dash **2.2081**. Jump a little soon — EarlyBuffer holds the press and fires on the first physics tick a shadow sim still calls safe. Assist, not autoplayer. No late-forgiveness (it does not rewind time for late clicks).

## Cloud Configs

Open **Settings → Assist → Assist Actions → Cloud Configs**, directly below **Import Config**. Browse and search community packs, sort by popularity or rating, and install settings. Keybinds and developer settings are preserved. Share submits your pack for review. Cached packs and bundled defaults support offline use. Installing a config can change your assist settings. See [Cloud Configs](docs/cloud_configs.md) for endpoints and network behavior.

| | |
|---|---|
| Mod ID | `audesign.earlybuffer` |
| Version | **1.5.1** |
| Target | GD **2.2081** / Geode **5.11.1+** |
| Platforms | Windows, macOS, iOS, Android (multi-platform `.geode` — must include `android64.so`; iOS needs `.ios.dylib`) |
| License | [MIT](LICENSE) |
| Repository | [ToldByNun/earlybuffering-gd](https://github.com/ToldByNun/earlybuffering-gd) |

> **Last 2.2081 line.** **v1.5.1** still targets Geometry Dash **2.2081** only (Live hitch / Wave IW / orbs-under-IW, Adaptive Prediction Planner, Cloud Configs). Newer GD versions need a separate port / major bump. A Windows-only local build will **not** load on Android or iOS — use a merged / checked package (`INSTALL.md`: `--require android64` / `--require ios`).

## What it does

- **Live / Hybrid** — plan each click with a shadow physics sim (threat zones, optional orb windows). Live can use the **Adaptive Prediction Planner** (`adaptive-plan`) with classic fallthrough. Hopeless clicks still pass through; your input is never blocked.
- **Replay / BruteForce** — bake click *windows* from an imported macro or a serial main-thread search. These are timing maps, not a live multi-click bot queue.
- **Trigger World Bake** — cache spawn-timed static mover / toggle poses so the shadow sim can collide them instead of treating every mover as unknown (see settings + [`docs/assist_reference.md`](docs/assist_reference.md)).
- **Safe Mode** — when assist is on (or has touched a run), progress / bests / percentage are not saved as legit clears.
- **Scope gates** — platformer hard-off; optional practice / startpos / percent window; up to six float “assist off” percent ranges.
- **Learning** — optional per-level bias / trust (off by default; never changes your tick window; does not self-reinforce from inject “saves”).
- **Self-bake** — optional practice (and normal) recording of vanilla clicks into the bake map (off by default), with idle merge/refine.

Full planner / settings reference: [`docs/assist_reference.md`](docs/assist_reference.md).

## Install

> **Not on the Geode Index** (banned). Install from GitHub only.

1. Install [Geode](https://geode-sdk.org/) for **GD 2.2081**.
2. Download the `.geode` from [Releases](https://github.com/ToldByNun/earlybuffering-gd/releases) (must contain `audesign.earlybuffer.android64.so` for Android; `audesign.earlybuffer.ios.dylib` for iOS).
3. Ensure dependency **geode.node-ids** ≥ v1.23.3 (that dependency can still come from the Index).

**Mobile (Android + iOS):** same save-dir I/O (`replay-drop/`, `earlybuffer-config.json`) and constrained planner budgets — details and device smoke checklists in [`INSTALL.md`](INSTALL.md).

## Build

```bash
# Requires GEODE_SDK and a C++23 toolchain (MSVC on Windows)
geode build --config RelWithDebInfo
```

Or configure CMake against `$env:GEODE_SDK` / `$GEODE_SDK`. See [`INSTALL.md`](INSTALL.md).

CI builds all platforms via [`.github/workflows/multi-platform.yml`](.github/workflows/multi-platform.yml).

## Settings (quick)

Open Geode → EarlyBuffer → Settings (or pause → **EB**). Highlights:

- **Enabled / Assist Mode / Window** — master control, Live / Hybrid / Replay / BruteForce, and tick windows (unit + default + wave) on **Main** (no Preset packs)
- **Pick Mode** (`assist-pick`) — **v1** (default); **v2** reserved (currently same as v1)
- **Assist Engine** (`assist-engine`) — **ShadowPlayer** (default) vs **Trajectory** for Live V2; v1 always ShadowPlayer
- **Safe Mode / Overlay / Pause UI** — publish-safe defaults; hide chrome when you want a clean pause menu
- **Trigger World Bake** — static mover cache for the shadow sim; optional skip of player-follow dynamics
- **Assist Off Ranges** — six From/To percent slots (`-1` = unused); assist forced off inside those ranges
- **Orbs / CPS / budgets** — Assist algorithm limits (ticks ≠ 60fps frames); Adaptive knobs under Assist when Adaptive Plan is on
- **Adaptive Plan** (`adaptive-plan`) — Live Prediction Planner (screen → verify → Gate); off = classic equal-walk
- **Save Feedback** — separate Sound / Color / Counter toggles when a delayed click is injected

Hotkeys are configurable (defaults include Ctrl+Shift+M assist mode, Ctrl+Shift+T trajectory, and more).

## Development status

EarlyBuffer **1.5.1** is an **actively maintained** release for **2.2081**: features are still being developed and optimized, and the build is exercised regularly (manual play + automated self-checks on mod load / PlanGold). Treat it as a production assist for this GD version — not an abandoned prototype — while remembering it will not track newer game builds.

## Authorship and AI use

**Product direction is human.** Feature goals, constraints, and iterative review come from the maintainer (Audesign).

**Implementation used AI coding tools** (Cursor and similar) as a drafting aid. **Every line shipped in this release was reviewed by the maintainer**, and behavior is **tested thoroughly and re-tested as features evolve** (playtesting, crash/lag probes, and automated self-checks). AI did not replace ownership, review, or QA.

Bug reports with GD / Geode / platform / steps (and crashlogs when relevant) are welcome.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for build tips, how to add settings / planner behavior, and PR expectations.

## Support and ethics

- This mod **assists inputs**. Using it on rated / online content may violate community norms or RobTop’s rules — use **Safe Mode**, practice, and your own judgment.
- Discord: [https://discord.gg/QJuTDkmPcf](https://discord.gg/QJuTDkmPcf)
- Issues: [GitHub Issues](https://github.com/ToldByNun/earlybuffering-gd/issues). In-mod support text: `support.md`.
- **Changelog:** see [`changelog.md`](changelog.md) — **v1.5.1** Live hitch / Wave / orbs-under-IW; **v1.5.0** Adaptive Prediction Planner; **v1.4.3** Cloud Configs (still GD **2.2081** only).

## Credits

- Maintainer / design / engineering: **Audesign** ([ToldByNun](https://github.com/ToldByNun))
- Built on [Geode](https://geode-sdk.org/) and Geometry Dash bindings
- Replay import uses [GDReplayFormat](https://github.com/maxnut/GDReplayFormat) (libGDR)

Working-tree / early development also used the names *frirecracker* and *EarlyGrace*; the published mod is **EarlyBuffer** (`audesign.earlybuffer`).
