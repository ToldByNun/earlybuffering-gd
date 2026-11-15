# AP01 baseline status

The frozen baseline contains all four original sessions, configurations, manifests and reports, with SHA-256 hashes of their original bytes. It retains every event, witness, timing phase, candidate and attempt evaluation. The companion case index contains all 180 finished plans and their source coordinates. Candidate results remain recorded simulations. Each intervention opportunity is unresolved pending counterfactual validation.

Run from the ship worktree:

```powershell
python tools/freeze_live_baseline_ap01.py
python tools/test_recorded_misses_ap01.py
```

Both checks passed on 2026-11-15. The first validates the retained event identities, counts and evidence classification. The second retains the five existing reproduced miss/control cases. These checks establish fixture integrity and prior recorded expectations.

A correct intervention must change an immediate miss/death into correct target activation or local threat clearance within the configured window. It needs matching player/world state, phase and subsequent physical inputs, plus primary and tail survival. An immediate safe activation is a control. A truncated candidate is unresolved. An attempt-ending death does not attribute harm to every preceding assist.

Priority reproductions are FOREST Cube attempt 14 tick 42 plan 157, attempt 20 ticks 39/123, the four observed Cube orb delays, all Ball threat delays and budget passes, Firework's two Ship passes, and Zodiac's observed successful arms. The case index preserves all remaining decisions to prevent selecting only favorable examples.

The existing evaluator calls ShadowSimulator.evalDelayFromFreeze and classifies restored state through EbDevStateDiff. The four new sessions classify their evaluations as playerOnlyApproximated. Their witnesses include partial orb/collision state and absent trigger state. A standalone Python sweep cannot reconstruct native Geometry Dash physics and mutable world state from those fields. No new parity-verified opportunity or harm count is established yet.

AP01 remains open for an in-game replay with the matching level revision, complete mutable world state, original step schedule and subsequent physical inputs. First reproduce the unchanged outcome, then sweep delays 0–8 at the recorded phase and capture target activation, landing and survival. Cases with drift stay unresolved. Preserve the existing approximate sweeps as diagnostic evidence. A build/install is required when native replay instrumentation changes.

User decision: proceed using recorded observations and approximate candidate evidence. Replay parity is no longer a prerequisite for AP02 or subsequent implementation. Opportunity and harm claims retain their recorded evidence classification.
