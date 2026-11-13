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

**v1.6.0** keeps the v1.5.1 planner contracts (AP0–AP1 / AP3a–AP3b / AP4: hitch / remUs / Wave IW floor + help-only/prox / orb-under-IW) and adds the desktop ImGui shell + deflate-only ship packages. Deferred from 1.5.1: AP2 landed ranking, AP3c micro-window.

## Adaptive smoke

`tools/test_adaptive_parity_smoke.cpp` — fallthrough Gate + AP1 `adaptive-zero-wall-clamped` / `fallthrough-remus-floor` + Assist Pack AP1 A4 fair-depth mirrors.

Assist Pack AP1 before/after:
- Frozen fixture: `tools/fixtures/fair_depth_ap1_a4.json` (Silent Hoveropolis A4 from `research_20261113_001322`).
- Offline policy smoke: `tools/test_fair_depth_ap1.cpp` (depth trunc / Hopeless block / ledger `budget-not-reached`).
- Pack KPI compare: `python tools/score_assist_pack_fair_depth.py --baseline <old_pack> --candidate <new_pack>` (primary: `hopelessWithAliveShort`, `notViableAliveShort`, `underselectZero`, `caused`, `budgetHitWithSurvivors`).

Assist Pack AP2 (Screen→Verify steal): same scorer — expect `budgetHitWithSurvivors` ↓ and `stolenPresses` / `adaptiveStolenScreenTicks` ↑ on survivor presses; PlanGold `assist-pack-ap2-screen-steal`.

Assist Pack AP4 (False Hopeless): Gate selfCheck A4 shallow-Alive → `PassBudget`; A7 uniform death stays Hopeless; PlanGold `assist-pack-ap4-false-hopeless`; pack KPI `hopelessWithAliveShort` ↓.

Assist Pack AP3 (Weak d0-safe, tightened): only survive/endX lead blocks sticky d0-safe (bare under-horizon does not — `research_20261113_023017` starved Delay). PlanGold `assist-pack-ap3-weak-d0safe`.

Assist Pack AP5 (endX/λ survivors): continuous IW ranks by endX not earliest; Wave save score peak; landed intended prefer; classic clear endX lead vs d0. PlanGold `assist-pack-ap5-endx-rank`.

Assist Pack AP6 (Harm Guard): **discarded** — starved Delay arms vs assist-feel; InterveneGate at pick remains.

Assist Pack AP7 (IW observation floor): discrete floor = `model.cap` (not `limit+cap`); classic matches Adaptive `96−limit` clamp. Fixes cube early `pass-budget` with Alive later. PlanGold `assist-pack-ap7-iw-floor`.

Assist Pack AP3c (toggleable): `assist-probe-past-horizon` (extend prove before PassD0Safe) + `assist-micro-window` (tiny Max Assist endX prefer). PlanGold `assist-pack-ap3c-toggles`. Both off ≈ pre-AP3c feel.

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
