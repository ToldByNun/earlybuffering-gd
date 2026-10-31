# Assist contracts v1.4.0 (LOCKED — Commit 2)

**DO NOT RETUNE** these thresholds without a new mod version and a new contracts document.

Corpus label for this freeze: `corpus=solo` (solo play + PlanGold). Multi-player opt-in reports may use `corpus=multi` with higher `A_min`.

Prev / Caused / Net / HarmRate_sim are **`simEstimate` only** — never a ship Pass/Fail by themselves (G10).

**Baseline status:** ε and G5–G7b numbers below are the **frozen Commit-2 LOCK** (Commit-1 probe placeholder + contract table). Live corpus p99 remeasure is deferred to a **new version**; until then runtime only **records** `max|dxy|` via `AssistRealMetrics` and must not change ε. `dev-freeze-world` default = **true**.

## Locked ε (GhostParityProbe)

| Symbol | Value | Source |
|--------|------:|--------|
| `εx` | **12 px** | Commit-1 baseline placeholder frozen as Commit-2 LOCK (`kLockedEpsX`) |
| `εy` | **12 px** | same (`kLockedEpsY`) |

Remeasure from corpus p99 of `|Δ|` on known-good static runs only in a **new version**. Runtime `AssistRealMetrics` tracks `max|dxy|` for that remeasure; it must not silently change ε.

## Sample sizes

| Symbol | Solo (`corpus=solo`) | Multi |
|--------|---------------------:|------:|
| `A_min` | **40** fired Delays | **100** |
| `Opp_min` / level | **20** opportunities | **20** |

If `A < A_min` or `N_opp < Opp_min` → gate status **`INCONCLUSIVE`** (not green).

## Real metrics (primary ship gates)

| Symbol | Definition |
|--------|------------|
| `N_opp` | Opportunity presses (trust ignored) |
| `N_trust_opp` | Opportunity ∧ `SimTrust.okAt(x)` |
| `TrustRate` | `N_trust_opp / max(1,N_opp)` |
| `A` | Delay / HUS→Delay armed |
| `AssistCov` | `A / max(1,N_trust_opp)` |
| `DAF` | Real death after a **fired** Delay inject |
| `DAFRate` | `DAF / max(1,A)` |
| `ParityOKRate` | Parity probes without drift |

`DAFRate` ship check uses **Wilson 95% CI upper bound**, not the point estimate alone.

## Frozen gates G4–G10

| Id | Rule (LOCKED) |
|----|----------------|
| **G4** | Long horizon only when trusted (bool policy; enforced in later PerfectAssist commits) |
| **G5 Real-Harm** | Wilson CI upper(`DAFRate`) **≤ 2%** when `A ≥ A_min` |
| **G6 Parity** | `ParityOKRate` **≥ 99%** on static corpus probes |
| **G7a AssistCov** | **≥ 95%** on `N_trust_opp` |
| **G7b TrustRate** | **per level**: static ≥ **70%**, dynamic-labelled ≥ **10%**, with `N_opp ≥ Opp_min` |
| **G8 Budget** | P95 plan ≤ budget; budgetHit ≤ baseline + **10%** rel (baseline recorded later) |
| **G9 Spot** | Spot-untrusted after drift → hard Pass until clear (Commit 1) |
| **G10 Sim-Diag** | Prev/Caused report only (`simEstimate`); **no** ship Pass/Fail |

### Anti-gaming

Tightening trust to shrink `N_trust_opp` and inflate AssistCov **fails G7b** when TrustRate drops. Dynamic / Enter-X / Touch / Follow segments are expected to show **low TrustRate** and more Pass — that is honesty, not a win.

## Code

- Constants + Wilson + gates: `AssistContracts140`
- Runtime counters / report line: `AssistRealMetrics`
- PlanGold: `assist-contracts-140`, `assist-real-metrics`
