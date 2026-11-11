# Contributing

For v1.5.1 Adaptive / Live IW / Cloud Config changes, verify Adaptive Plan Live Delay vs classic fallthrough, Wave help-only / orb-under-IW, the Settings → Assist Cloud Configs entry, catalog loading, config installation, settings refresh, sharing failures and offline fallback. Submission credentials belong in server environment variables. See [Cloud Configs](docs/cloud_configs.md) and Adaptive notes in `docs/assist_reference.md`.

Thanks for helping with **EarlyBuffer**. Please read the [README](README.md) development / authorship notes: direction and review are human-led; AI may assist drafting. Prefer small, testable PRs.

**Scope note:** **v1.2.1** targets **GD 2.2081** only (last line for that game build). Work for newer GD versions belongs in a dedicated port / major bump — do not mix incompatible bindings into this branch casually.

## Setup

1. Install Geode SDK + CLI; export `GEODE_SDK`.
2. Clone [earlybuffering-gd](https://github.com/ToldByNun/earlybuffering-gd); `geode build --config RelWithDebInfo`.
3. Prefer Windows for day-to-day iteration (matches primary hooks).

## Project layout

See [Source architecture](docs/source_architecture.md) for the full directory
structure and placement rules, including subdivisions of cloud, dev and UI.

| Path | Role |
|------|------|
| `src/config/` | `StageConfig` — settings cache / presets |
| `src/core/` | Domain modules; see [Source architecture](docs/source_architecture.md) |
| `src/hooks/` | `$modify` game hooks |
| `src/ui/` | Overlay, pause hub, trajectory, save feedback |
| `src/debug/` | Assist log, LagProbe, PerfTrace, PlanGoldCorpus |
| `mod.json` | Geode metadata + settings UI |
| `docs/assist_reference.md` | Planner / settings contract |

## Adding a setting

1. Add the entry under `settings` in `mod.json` (prefer `bool` / `int` / `float` / `string`+`one-of`).
2. Cache it in `StageConfig::refreshCache()` and expose a getter.
3. `listenForSettingChanges<…>` in `src/early_buffer_module.cpp`.
4. Document the key in `docs/assist_reference.md` and mention it in `changelog.md` if user-facing.
5. Wire behavior in the smallest core/hook file that owns the concern.

## Changing the planner / trigger world

- Contract lives in `docs/assist_reference.md` (lexico, orb window, budgets, buffer SM, trigger bake lifecycle).
- Prefer deterministic tick budgets over wall-clock-only logic.
- Keep bake **main-thread / serial** (`BakeScheduler` / `TriggerWorldBaker::tickSlice`); do not reintroduce worker-pool Cocos use.
- Extend `PlanGoldCorpus`, `TriggerClassify::selfCheck`, `TriggerWorldStore::selfCheck`, and/or `ShadowSimulator::regressionSelfCheck` when you add invariants.
- Platformer must stay assist-off.
- Classes + `#ifndef` guards; **no** project namespaces / anonymous namespaces for new code; prefer `this->` on members.

## PR checklist

- [ ] Builds (`geode build` or CI green)
- [ ] No drive-by renames / unrelated formatting
- [ ] Settings + docs updated together
- [ ] Honest wording for Replay/Brute/Safe Mode / Trigger World if you touch those paths
- [ ] Short PR description: *why*, not only *what*

## Code style

- Follow [AGENTS.md](AGENTS.md) for class layout, early returns and variable naming.
- Use paired `snake_case.hpp` / `snake_case.cpp` files with include guards.
- Use explicit dependency qualification and class members for project helpers and state.
- Format with `.clang-format` and run `python tools/check_cpp_style.py`.
- Avoid committing `build/`, `build_*.txt`, or local save dumps.

## License

By contributing, you agree your changes are licensed under the MIT License (see `LICENSE`).

## Releases

Bump `mod.json` + `CMakeLists.txt` version together. Local staging may live in `releases/` (gitignored). Do not commit that folder.
