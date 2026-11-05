# Assist contracts v1.4.2 (UNFROZEN)

**Status: UNFROZEN** — placeholder thresholds only. Do not ship Pass/Fail on R1–R8 until a measured corpus fills this sheet and a later freeze sets `AssistContracts142::kUnfrozen = false`.

Do **not** retune [`assist_contracts_140.md`](assist_contracts_140.md) / `AssistContracts140` numbers here. That sheet stays **LOCKED**. εx/εy = 12 px remain frozen.

Corpus label for measurement: `corpus=solo` (solo play + PlanGold). Multi-player opt-in may use `corpus=multi` with higher `A_min`.

## What this version measures

| Symbol | Definition |
|--------|------------|
| `N_opp` / `N_trust_opp` / `A` / `DAF` | Same as v1.4.0 real metrics |
| **Per `GameModeKind`** | Same counters split by mode at opportunity / arm / DAF |
| `AssistCov` / `TrustRate` / `DAFRate` | Global + per-mode in `AssistRealMetrics::reportLine` |

Assist behavior is **unchanged** while this stub is UNFROZEN — instrumentation only.

## ModeAssist (parity hierarchy)

Per-`GameModeKind` policy lives in `ModeAssist` / `ModeAssistSnapshot` (DiscreteJump / FeatherContinuous / TapContinuous / Robot / Wave leaves). `AssistHorizon`, `ClickIntentGuard` spam tables, `AssistController` continuous/release + Wave spam-λ, `ShadowSimulator::modelFor` continuous/tapRelease, and `PrefixSim` kind gates thin-wrap it. Hot path: fill `PlanOptions.modeSnap` once per press. PlanGold: `mode-assist`. Tune numbers later — this step is structure + parity only.

## SpamAssist (ShiftedInputQueue)

Follow-up presses/releases during an armed Delay share one `shift` (`ShiftedInputQueue`, Cap 8). `SpamAssistPolicy` uses ModeAssist gaps + `featheringIsControl` (Ship/Swing density ≠ PassSpam). Trust+opportunity / orb / bake still AllowAssist. PassSpam with a non-empty queue drains 1/tick (no drop). Overflow → `pass-queue-overflow`. `min-click-interval` does not block enqueue while shift is live. Overlay: `queue=N shift=K`. PlanGold: `shifted-input-queue`, `spam-assist-policy`. No auto-click/release.

## Sample sizes (placeholders)

| Symbol | Solo | Multi |
|--------|-----:|------:|
| `A_min` | 40 | 100 |
| `Opp_min` / level | 20 | 20 |
| `A_min` / mode (solo) | 12 | — |

If samples are below mins → **INCONCLUSIVE** (not green). While `kUnfrozen`, R-gates always report **INCONCLUSIVE** even with large A.

## Gates R1–R8 (placeholders)

| Id | Metric | Ship rule (placeholder until measured) |
|----|--------|----------------------------------------|
| **R1** | `AssistCov` on `N_trust_opp` | ≥ measured baseline−ε (stub uses G7a 95%) |
| **R2** | `TrustRate` | Anti-gaming like G7b (static ≥70% / dynamic ≥10%) |
| **R3** | Wilson UB `DAFRate` | ≤ 2% (G5) or measured max |
| **R4** | `RouteDirectionHit` | report-only until Route ships |
| **R5** | `OffRouteFallbackRate` | report-only first, then cap |
| **R6** | `PathStaleRate` | report-only |
| **R7** | P95 press plan µs | ≤ `sim-budget-us` (+slack placeholder) |
| **R8** | Cache load P95 | hitch budget (chunk like TW) |

R4–R8 stay report-only stubs until Route / cache / perf counters are wired.

## Corpus checklist (fill after sessions)

Measure before any Route / ModeAssist / SpamAssist behavior change. Per-mode splits must cover Cube, Ship, and Ball at minimum.

| Level / fixture | Modes exercised | Notes |
|-----------------|-----------------|-------|
| The Golden | | |
| The Golden 2 | | |
| Sevvend Clubstep | | |
| GRIEF | | |
| tg2 Startpos (`Tg2BakeFixture`) | | |
| 1 Wave open-gap | Wave | |
| 1 Trigger-heavy | | |

Paste `AssistRealMetrics::reportLine()` output (includes `contracts142=UNFROZEN` and `modes=[…]`) into the session log. Mark each mode with `A < A_min/mode` as **INCONCLUSIVE**.

## Baseline freeze (later)

When the corpus is filled:

1. Copy measured R1–R3 (+ per-mode mins) into this doc and `AssistContracts142` constants.
2. Set `kUnfrozen = false` / `freezeStatus() → LOCKED`.
3. Never retune without a new mod version + new contracts document.

## Code

- Constants + gates: `AssistContracts142` (`kUnfrozen == true`)
- Runtime counters / report line: `AssistRealMetrics` (global + per-mode)
- PlanGold: `assist-contracts-142`, `assist-real-metrics`
- Locked prior sheet: `AssistContracts140` / `docs/assist_contracts_140.md`
