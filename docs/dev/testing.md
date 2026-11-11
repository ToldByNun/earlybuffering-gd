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

1.5.1 AP0/AP1: `live-plan-contracts` + `live-hitch-ceiling` (`EbLivePlanContracts`) — hitch ceiling / remUs floor wired; landed ranking + orb-under-IW still current vs intended until AP2–AP3.

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
