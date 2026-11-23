# Planner pipeline

This is the mental model for Live presses. Full contract: [Assist reference](/assist_reference).

## Entry

`ShadowSimulator::plan` (and Adaptive wrapper when `adaptive-plan` is on) builds an `EbSimPlan`:

- `action` = Pass or Delay
- `delay` = ticks to hold
- `reason` = AssistReason / verdict
- optional `candNotes` for Dev capture

## Normal Live rescue

`strictLiveRescue` bypasses Adaptive and the legacy scoring path. It preserves a
fully checked d0, otherwise schedules coarse delay probes followed by local
refinement, with exhaustive untested-tick fallback if no coarse survivor exists.
All accepted probes reach one common absolute target, fixed before d0. Results
are cached only within this search; unknown/truncated probes are never safe.

`EbLiveHorizonController` adapts automatic horizons between searches (0.85 on
budget exhaustion, 1.03 recovery under half budget, 0.5..1 range). It preserves
the full Max Assist window and explicit mode/look-ahead caps. State is isolated
by player, mode and edge, and resets on level entry.

The ShadowPlayer path reuses an idle-prefix cursor and a reusable fork snapshot.
Backwards refinement reseeds the cursor; dual, Trajectory and initial Wave-slope
cases retain independent simulation. Optional jitter uses independent probes.
The opt-in `assist-verify-ab` compares shared candidates against independent GD
runs and keeps the independent result for those diagnostic runs.

Traces include `liveObservationTicks` and `sharedIdleTicksSaved`; the existing
`physicsCalls`, `planUs` and phase timing fields support real before/after
measurements. The offline broad-window test measures scheduling counts only,
not actual game frame times.

## Legacy / Hybrid candidate path (simplified)

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
- Candidate fill: d0 via `evalDelay`, then **idle-prefix share** for 1..limit (legacy per-delay fill for gaps / dual / traj)

Stopping at first safe on ball was a real footgun (Eval `bestSurv` ≠ Live pick).

Ball `stopAtBallLanding`: do **not** rank landed cands by absolute `surviveBin` — ticksRan grows with delay.

**1.5.1 / 1.6.x contracts** (`EbLivePlanContracts`, PlanGold `live-plan-contracts` / `live-hitch-ceiling`; AP-L1–L7 on **1.6.2**):

| Topic | Status |
|---|---|
| Soft wall `simUs=0` | **AP1:** classic + Adaptive → 8ms hitch ceiling |
| Fallthrough remUs | **AP1:** floor 2000µs when ticks remain |
| Peak-death stop | **AP1:** `skip=peak-death-cut` only (no fake-dead) |
| Wave continuous IW cap | **AP3a:** `max(lookAhead, short AssistHorizon)`; Wave ≥32 |
| Wave help-only / prox | **AP3b:** d0 Alive → Pass; proximity/futile on IW; earliest clear Delay |
| Orbs under IW | **AP4:** spatial hint + budget-capped scan; DelayOrb handoff; dash/toggle |
| Micro-window / false d0-safe | **AP3c:** `assist-micro-window` + `assist-probe-past-horizon` (each off = prior 1:1) |
| Landed rank | Later only if score gain >8px → **AP2 (skipped for now):** Eval-aligned |

Live press always takes input-window (`requireAliveForDelay`) — G1 AssistPick orb band is bypassed until AP3.

### Same-crash cut

If several delays die on the **same** `deathTick`, PrefixSim can stop searching — later delays are unlikely to help that obstacle.

### Prefer-band early exit

Bake prefer windows can exit early when robust — **not** when orb foresight still needs the rest of the band.

## Adaptive Live

When `adaptive-plan` is on:

1. **Screen** delays with short quanta  
2. **Verify** promising delays (resume / rebuild checkpoints)  
3. **DecisionGate** — honest Pass/Delay  
4. Else **classic fallthrough** on the **same** press soft wall (`reusePlanWall` + leftover ticks; AP-L5 — not a second full envelope / no remUs floor stack)

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

## Per-tick cost notes (1.5.1 / 1.6.x)

- Group membership for capture is a sidetable built in `beginCapture`
- TW apply caches last written pose on `SavedTransform` and skips redundant writes
- `hideGhost` is session/run scoped — not inside `stepPhysics` every collide/update
