# Planner pipeline

This is the mental model for Live presses. Full contract: [Assist reference](/assist_reference).

## Entry

`ShadowSimulator::plan` (and Adaptive wrapper when `adaptive-plan` is on) builds an `EbSimPlan`:

- `action` = Pass or Delay
- `delay` = ticks to hold
- `reason` = AssistReason / verdict
- optional `candNotes` for Dev capture

## Classic Live (simplified)

```
prepare (inputWindow? budgets? orb scan?)
  → input window path? (requireAlive + down/continuous)
  → else threat probe (d0, opportunity, wave proximity)
  → candidate search (delay order, same-crash cut, prefer-band exit)
  → selection (Pick v1 / v2 / orb band)
  → publish ledger + plan
```

### Input window

When `requireAliveForDelay` and (press down or continuous control):

- Viability = `survivesObservation` (alive / unknown / landing rules)
- **Continuous** (wave/ship/swing): earliest safe, then stop
- **Discrete** (cube/ball/…): scan the band, pick survive_then_endx (+ latency)

Stopping at first safe on ball was a real footgun (Eval `bestSurv` ≠ Live pick).

### Same-crash cut

If several delays die on the **same** `deathTick`, PrefixSim can stop searching — later delays are unlikely to help that obstacle.

### Prefer-band early exit

Bake prefer windows can exit early when robust — **not** when orb foresight still needs the rest of the band.

## Adaptive Live

When `adaptive-plan` is on:

1. **Screen** delays with short quanta  
2. **Verify** promising delays (resume / rebuild checkpoints)  
3. **DecisionGate** — honest Pass/Delay  
4. Else **classic fallthrough** with **remaining** tick/µs budget (not a second full envelope)

Do **not** re-inflate “peak” survive ticks across verify segments — current quantum is the truth.

## Hybrid

Bake `findNear` → `BakeVerifier` → only trust if clears threat and **not** `budgetHit`. Verify + Live share one press envelope with a Live reserve.

## Budgets

| Knob | Role |
|---|---|
| `tick-budget` | Hard sim-tick allowance per press |
| `sim-budget-us` | Soft wall clock (`<=0` can mean unlimited wall — be careful) |

`budgetHit` ⇒ weaker search / PassBudget paths — never soft-trust a bake on budget exhaustion.

## Wave regression

When changing input planning, run the recorded Wave regression the project expects (`AGENTS.md`).
