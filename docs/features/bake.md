# Trigger World & Moving Objects

Trigger World preparation supplies moving-object and trigger state to the Live simulator. Its controls are under Assist.

On smaller levels, Trigger World can bake static movers to a compact cache (`.ebtw` preferred; legacy JSON migrates under a size cap).

On large / high-trigger levels, EarlyBuffer skips multi‑100MB disk parses and uses a **Nova-style Moving Objects** path instead: record nearby movers during attempts, then predict from the attempt timeline or ≤0.5s motion guess. That data is session-only and cleared on quit.

The former Bake tab, Replay and BruteForce assist modes, macro import actions, bake path visualizer and self-bake controls have been removed. Existing files remain on disk.

Hybrid uses the level’s learned click timings with Auto-Tune enabled.
