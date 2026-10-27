# EarlyBuffer

**Geode** mod for **Geometry Dash 2.2081** -- early-input forgiveness assist.

When you click a little early, EarlyBuffer buffers the press and fires on the first shadow-simulated safe tick. It is an assist tool, not an autoplayer and not late-forgiveness (it does not rewind time for late clicks).

| | |
|---|---|
| Mod ID | `audesign.earlybuffer` |
| Target | GD 2.2081 / Geode 5.11.1+ |
| Platforms | Windows, macOS, iOS, Android (CI) |
| License | [MIT](LICENSE) |

## What it does

- **Live / Hybrid** -- plan each click with a shadow physics sim (threat zones, optional orb windows). Hopeless clicks still pass through; your input is never blocked.
- **Replay / BruteForce** -- bake click *windows* from an imported macro or a serial main-thread search. These are timing maps, not a live multi-click bot queue.
- **Safe Mode** -- when assist is on (or has touched a run), progress / bests / percentage are not saved as legit clears.
- **Scope gates** -- platformer hard-off; optional practice / startpos / percent window; up to six float "assist off" percent ranges.
- **Learning** -- per-level bias / trust (never changes your tick window; does not self-reinforce from inject "saves").

Full planner / settings reference: [`docs/assist_reference.md`](docs/assist_reference.md).

## Install

1. Install [Geode](https://geode-sdk.org/) for GD 2.2081.
2. Download the latest `.geode` from [Releases](https://github.com/ToldByNun/frirecracker/releases) (or build locally).
3. Drop it into your Geode mods folder, or use the `geode` CLI.

Dependency: **geode.node-ids** >= v1.23.3 (Geode will prompt).

## Build

```bash
# Requires GEODE_SDK and a C++23 toolchain (MSVC on Windows)
geode build --config RelWithDebInfo
```

Or configure CMake against `$env:GEODE_SDK` / `$GEODE_SDK`. See [`INSTALL.md`](INSTALL.md).

CI builds all platforms via [`.github/workflows/multi-platform.yml`](.github/workflows/multi-platform.yml).

## Settings (quick)

Open Geode -> EarlyBuffer -> Settings. Highlights:

- **Enabled / Preset / Assist Mode** -- master control and Live / Hybrid / Replay / BruteForce
- **Safe Mode / Overlay / Pause UI** -- publish-safe defaults; hide chrome when you want a clean pause menu
- **Assist Off Ranges** -- six From/To percent slots (`-1` = unused); assist forced off inside those ranges
- **Tick window / orbs / CPS / budgets** -- timing and CPU limits

Hotkeys are configurable (defaults include F6 preset cycle, Ctrl+Shift+M assist mode, and more).

## Honesty: authorship and AI use

**Product direction is human.** Feature ideas, constraints ("no midpoint", main-thread bake only, percent disable ranges, Safe Mode rules, honest Replay/Brute wording, and so on) and iterative critique come from the maintainer.

**Implementation and docs are AI-assisted.** Substantially all C++, `mod.json`, README, and reference docs in this repository were written with AI coding tools (Cursor) under that direction -- not as unaided hand-authored code.

Please treat the project accordingly: review PRs carefully, prefer small diffs, and do not assume every line has been line-by-line human-audited for every edge case. Bug reports and careful reviews are especially welcome.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for build tips, how to add settings / planner behavior, and PR expectations.

## Support and ethics

- This mod **assists inputs**. Using it on rated / online content may violate community norms or RobTop's rules -- use Safe Mode, practice, and your own judgment.
- Issues: GitHub Issues. Geode in-mod page: `support.md`.

## Credits

- Maintainer / design: **Audesign** ([ToldByNun](https://github.com/ToldByNun))
- Built on [Geode](https://geode-sdk.org/) and Geometry Dash bindings
- Replay import uses [GDReplayFormat](https://github.com/maxnut/GDReplayFormat) (libGDR)

> Formerly developed under the working names *frirecracker* / *EarlyBuffer*.