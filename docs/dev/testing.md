# Tests & style

## Style

```bash
python tools/check_cpp_style.py
```

Format C++ with the repo `.clang-format`.

## Self-checks

Many modules expose `selfCheck` / regression helpers loaded or invoked from debug / PlanGold paths. When you add an invariant, extend the matching check (examples called out in [Assist reference](/assist_reference)):

- Adaptive gate / parity / budget controllers
- Trigger classify / TriggerWorld store
- ShadowSimulator regression
- Reject ledger classification

## PlanGold

`src/debug/plan_gold_corpus.cpp` — synthetic planner gold cases. Extend when Pass/Delay contracts change.

1.5.1 AP0–AP3b: `live-plan-contracts` + `live-hitch-ceiling` + `wave-horizon-floor` + `wave-iw-help-only` / `wave-iw-prox-pass` — hitch / remUs / Wave IW floor + help-only/prox wired; landed ranking + orb-under-IW until AP2/AP4.

## Adaptive smoke

`tools/test_adaptive_parity_smoke.cpp` — fallthrough Gate + AP1 `adaptive-zero-wall-clamped` / `fallthrough-remus-floor` (link `live_plan_contracts.cpp`).

## Docs site

```bash
npm install
npm run docs:dev
npm run docs:build
```

## Wave regression

Required when changing or refactoring input planning (`AGENTS.md`).

```bash
g++ -std=c++23 tools/test_wave_input_window.cpp \
  src/core/assist/contracts/miss_assist_contracts.cpp \
  src/core/assist/contracts/assist_verdict.cpp \
  -o build/test_wave_input_window.exe
```

AP3a uses horizon `limit + max(lookAhead, Wave short planCap≥32) + proveTicks`. Recorded rows 88/114 expect Pass under the floor; legacy short-horizon dual assert still locks earliest-safe d=2/d=4.
