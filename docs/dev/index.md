# Developer section

For people who want more than “set Window to 4”.

Player docs stay simple. This section is the **map into the codebase**, planner, traces, and ship process.

## Start here

| Page | When you need it |
|---|---|
| [Build & package](/dev/build) | Compile, multi-platform `.geode`, package checks |
| [Source layout](/dev/architecture) | Where code lives (`assist/`, `sim/`, `dev/`, …) |
| [Planner pipeline](/dev/planner) | Live / Adaptive / classic fallthrough / input window |
| [Settings & mod.json](/dev/settings) | Adding a setting the right way |
| [Dev traces & reports](/dev/traces) | Sessions, candNotes, bestSurv, lag probes |
| [Tests & style](/dev/testing) | Self-checks, PlanGold, clang-format |
| [Contributing](/dev/contributing) | PR expectations |

## Long-form contracts (repo markdown)

These are dense on purpose:

- [Assist reference](/assist_reference) — planner / budgets / Adaptive / Trigger World
- [Cloud configs (tech)](/cloud_configs) — endpoints, cache, share
- [Source architecture](/source_architecture) — folder ownership rules
- [Assist contracts 1.4.0](/assist_contracts_140) — older contract snapshot

## Distribution note

EarlyBuffer is **banned from the Geode Index**. Shipping = GitHub Releases / direct `.geode`, not Index publish. See [Install](/install/).

## Branch tip

Active 1.6.x work often lives on `earlybuffer-1.6.x-dev` branches. Docs edit link points at `earlybuffer-1.6.0-dev`.
