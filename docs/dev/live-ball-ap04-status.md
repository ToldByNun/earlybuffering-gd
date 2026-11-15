# AP04 Ball landing and orb intent

Implemented directly in the Live input-window planner for GD 2.2081.

Ball now discovers an orb window before full landing-band search when orb assist is enabled and no window is cached. Discovery, candidate simulation and ordinary landing candidates share the existing tick and frame wall budget. The late fallback scan after a complete ground search was removed.

Orb selection consumes already verified activated candidates independently of ground-band completeness or another candidate's stable landing. A safe immediate grounded flip that does not activate a ring keeps ordinary landing selection. An airborne input with a reachable window and a verified activation can select the ring. Mere proximity without verified activation cannot select it. Orb-assist disabled and linked-dual verification retain their existing paths.

The grounded-immediate policy is a deterministic intent heuristic. Native in-game outcomes on moving platforms, incidental airborne rings and overlapping rings remain unmeasured. A ring discovered outside the current activation window cannot become a verified target under this policy.

Regression checks passed for both initial gravity directions, residual ground flags, interrupted contact, stable contact across checkpoints, gravity reversal, disabled orb assist, absent reachable window, missing activation, airborne orb intent and safe immediate grounded flip. Recorded Wave regression and C++ style passed. Runtime gameplay validation remains pending.

Final geode build succeeded and installed the package. Installed package SHA-256 matches the generated package.
