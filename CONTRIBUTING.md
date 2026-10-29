# Contributing

Thanks for helping with **EarlyBuffer**. Please read the [README](README.md) development / authorship notes: direction and review are human-led; AI may assist drafting. Prefer small, testable PRs.

**Scope note:** **v1.2.0** targets **GD 2.2081** only (last line for that game build). Work for newer GD versions belongs in a dedicated port / major bump — do not mix incompatible bindings into this branch casually.

## Setup

1. Install Geode SDK + CLI; export `GEODE_SDK`.
2. Clone [earlybuffering-gd](https://github.com/ToldByNun/earlybuffering-gd); `geode build --config RelWithDebInfo`.
3. Prefer Windows for day-to-day iteration (matches primary hooks).

## Project layout

| Path | Role |
|------|------|
| `src/config/` | `StageConfig` — settings cache / presets |
| `src/core/` | Planner, bake, trigger world, scope gate, learning, buffer |
| `src/hooks/` | `$modify` game hooks |
| `src/ui/` | Overlay, pause hub, trajectory, save feedback |
| `src/debug/` | Assist log, LagProbe, PerfTrace, PlanGoldCorpus |
| `mod.json` | Geode metadata + settings UI |
| `docs/assist_reference.md` | Planner / settings contract |

## Adding a setting

1. Add the entry under `settings` in `mod.json` (prefer `bool` / `int` / `float` / `string`+`one-of`).
2. Cache it in `StageConfig::refreshCache()` and expose a getter.
3. `listenForSettingChanges<…>` in `src/main.cpp`.
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

- Match neighboring files (Geode prelude, class-only headers with `#ifndef` guards).
- Avoid committing `build/`, `build_*.txt`, or local save dumps.

## License

By contributing, you agree your changes are licensed under the MIT License (see `LICENSE`).

## Releases

Bump `mod.json` + `CMakeLists.txt` version together. Local staging may live in `releases/` (gitignored). Do not commit that folder.
