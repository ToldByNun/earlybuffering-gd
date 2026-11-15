# AP03 implementation status

Single-input evaluation now precedes optional UFO continuation. Only a known lethal, ring-activated candidate with remaining budget may try a follow-up. A follow-up replaces the original result only when its continuation tick is verified. Successful isolated inputs avoid the extra simulation; failed or truncated speculative continuations preserve the original result.

The preferred activation-window search now includes Cube alongside UFO and Ball. It probes the recorded reachable window center-out before the later horizon and ascending-band work. Once an activation is verified, optional horizon-extension work is skipped to preserve the tested observation floor and dispatch budget. Linked dual remains on its existing partner-verification path. Continuation remains UFO-only and simulation-only.

Historical inspection confirms 833183c changed stable Ball landing tracking. 6ffc91c introduced UFO continuation and center-out orb probing. Its contact collection and continuation verification are UFO-specific; they cannot directly explain the recorded Cube misses. The Cube priority omission and almost universal budget exhaustion are addressed here as code-level mechanisms. The supplied recordings retain approximate evidence classification, per the decision to proceed without replay.

Passed checks: orb continuation including successful single-ring/no-contact/unknown/exhausted-budget eligibility, existing second-ring activation and 24-tick proof boundaries, center-out probe ordering, recorded Wave regression, and C++ style audit. Native compilation and install are recorded after the final build.

Remaining gameplay coverage includes isolated rings, overlapping rings, long chains, moving/toggled rings, dash holds and both players. Exact behavioral equivalence to the historical binary and recovered-opportunity counts have not been established by these offline checks.

Final geode build succeeded and installed the package. Build and installed package SHA-256 match. Geode bindings emitted existing duplicate-output warnings (MSB8027).
