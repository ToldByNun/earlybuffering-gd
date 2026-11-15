# AP02 implementation status

Implemented on the v1.6.2 ship worktree for GD 2.2081.

The main-thread frame envelope comes from acbc68e. Press, release, bake verification, buffered replan and hold verification share accumulated steady-clock time keyed by the director render frame. Nested scopes count elapsed work once. The configured budget is respected, including small values; zero selects the hitch ceiling. Each planning scope reserves 100 microseconds for exit work. Exhaustion prevents new simulation and allows normal input forwarding and cleanup.

Phase attribution comes from 105d995. Research serialization includes frame identity, configured frame budget, time spent before this plan, and cumulative physics world/update/collision costs. The cost reporter accepts these new fields and older packs without them.

Validation passed: frame budget regression, Adaptive parity smoke, recorded Wave input-window regression, and C++ style audit (480 files). Changed C++ was formatted with the repository clang-format configuration.

Remaining measurement work includes full-hook allocation/capture/dispatch cost, frame-time distributions under rapid dual input, recorder/overlay off/on comparison, cold/warm input, and actual in-game p95/p99 targets. This implementation bounds shared wall time for planning scopes. It does not establish a hard preemption limit for a single native operation. Tick limits remain per operation; the shared frame limit is elapsed time. The initial port uses 100 microseconds as an exit reserve pending runtime measurement.

AP01 evidence is accepted without replay at the user's direction. Recorded simulations retain approximate classification.

Build result: geode build succeeded and installed audesign.earlybuffer.geode into the configured Geometry Dash profile. Runtime latency measurements remain pending.
