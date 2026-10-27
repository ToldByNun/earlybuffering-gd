# Contributing

Thanks for helping. Please read the [README](README.md) honesty note: design is human-led; most of the tree is AI-assisted. Reviews and small, testable PRs matter more here than elsewhere.

## Setup

1. Install Geode SDK + CLI; export `GEODE_SDK`.
2. Clone this repo; `geode build --config RelWithDebInfo`.
3. Prefer Windows for day-to-day iteration (matches primary hooks).

## Project layout

| Path | Role |
|------|------|
| `src/config/` | `StageConfig` — settings cache / presets |
| `src/core/` | Planner, bake, scope gate, learning, buffer |
| `src/hooks/` | `$modify` game hooks |
| `src/ui/` | Overlay, pause menu, trajectory |
| `src/debug/` | Assist log, PlanGoldCorpus |
| `mod.json` | Geode metadata + settings UI |
| `docs/assist_reference.md` | Planner / settings contract |

## Adding a setting

1. Add the entry under `settings` in `mod.json` (prefer `bool` / `int` / `float` / `string`+`one-of`. Avoid free-text strings needing `.` `-` — Geode’s default filter is alphanumeric).
2. Cache it in `StageConfig::refreshCache()` and expose a getter.
3. `listenForSettingChanges<…>` in `src/main.cpp`.
4. Document the key in `docs/assist_reference.md` and mention it in `changelog.md` if user-facing.
5. Wire behavior in the smallest core/hook file that owns the concern.

## Changing the planner

- Contract lives in `docs/assist_reference.md` (lexico, orb window, budgets, buffer SM).
- Prefer deterministic tick budgets over wall-clock-only logic.
- Keep bake **main-thread / serial** (`BakeScheduler`); do not reintroduce worker-pool Cocos use.
- Extend `PlanGoldCorpus` / `ShadowSimulator::regressionSelfCheck` when you add invariants.
- Platformer must stay assist-off.

## PR checklist

- [ ] Builds (`geode build` or CI green)
- [ ] No drive-by renames / unrelated formatting
- [ ] Settings + docs updated together
- [ ] Honest wording for Replay/Brute/Safe Mode if you touch those paths
- [ ] Short PR description: *why*, not only *what*

## Code style

- Match neighboring files (Geode prelude, class-only headers with `#ifndef` guards).
- No `namespace Eg` / anonymous namespaces for new code (project convention).
- Avoid committing `build/`, `build_*.txt`, or local save dumps.

## License

By contributing, you agree your changes are licensed under the MIT License (see `LICENSE`).