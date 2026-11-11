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

1.5.1 AP0: `live-plan-contracts` (`EbLivePlanContracts`) — hitch ceiling, landed ranking current vs intended, peak-death skip contract, orb-under-IW current vs intended. Wire call sites in AP1–AP3 without relaxing these tables.

## Adaptive smoke

Offline helper mentioned in assist reference: `tools/test_adaptive_parity_smoke.cpp` (when present in tree).

## Docs site

```bash
npm install
npm run docs:dev
npm run docs:build
```

## Wave regression

Required when changing or refactoring input planning (`AGENTS.md`).
