# Source architecture

`src/` groups paired C++ headers and implementations by responsibility. Split a
folder when it contains several distinct responsibilities or becomes difficult
to scan. Keep cohesive small modules together; do not add a directory for every
class. The current leaf folders contain at most six implementation/header pairs.
Keep new files with the behavior they belong to, and revisit the split as the
project grows. File count is a review signal, not a mandatory numeric limit.

The root entry pair delegates module initialization. Small cohesive areas stay
flat: `debug/` contains four shared diagnostic pairs, and `hooks/` contains five
pairs grouped by hooked game subsystem. `config/` keeps the settings facade at
its root and cache loading under `cache/`. Larger areas are organized as follows:

| Area | Subdirectories and ownership |
| --- | --- |
| `cloud/` | `catalog/`: catalog model, offline seed and loading service; `transport/`: HTTPS client, host policy and SHA-256 integrity; `sharing/`: export, public submission and maintainer publishing. Apply, disk store and catalog statistics remain at the area root. |
| `dev/` | `capture/`: shared clock contract, cycle recording, live witnesses and trace persistence; `evaluation/`: replay evaluation, restoration, objectives, state differences and rejection attribution; `reporting/`: report generation and diagnostic dumps. Session orchestration and developer settings remain at the area root. |
| `ui/` | `settings/`: custom setting controls and filtering; `cloud/`: catalog and share popups with list cells; `pause/`: pause-menu attachment and action dispatch; `overlays/`: status and developer HUD; `visualizers/`: bake windows and trajectories; `feedback/`: save cues and frame markers. |

## Core modules

| Directory | Responsibility |
| --- | --- |
| `assist/` | Controller, clock and scope gate; `planning/` owns horizon, candidate ranking, opportunities, orb alignment and intervention policy; `contracts/` owns shared verdict and observation contracts; `input/` owns buffer transitions and click intent |
| `sim/` | Shadow simulation, shared prefixes, collision handling, survival evaluation, session guards and trust; `foresight/` owns long/travel prediction; `orbs/` owns orb-stack resolution and toggle-orb simulation |
| `bake/` | Bake maps, serial scheduling, verification, hybrid verification cache and self-bake recording |
| `trigger_world/` | Trigger classification, world baking and stored trigger-world data |
| `bundle/` | Configuration bundle model, serialized keys and codec |
| `learning/` | Adaptive assist learning, persistent level profiles and per-spot trust history |
| `replay/` | Replay format import and replay input data |
| `level/` | Level content identity and safe traversal of level objects |
| `platform/` | Device constraints and platform-specific save/drop paths |
| `diagnostics/` | Decision logs, performance and real-outcome metrics, counterfactual estimates, divergence and ghost parity probes |
| `fixtures/` | Recorded corpus cases and bake regression fixtures |

Keep each `.hpp` beside its matching `.cpp`. Use relative includes: sibling
filenames within a module and explicit relative paths across modules. Do not add
forwarding headers at the former flat paths or global include search paths that
hide the owning module.

These directories identify ownership within the existing core. They are not
independent libraries or enforced dependency layers: the controller, simulator,
bake lifecycle and diagnostic probes still share their existing contracts.
Game hooks remain in `src/hooks/`, presentation in `src/ui/`, developer tooling
in `src/dev/`, shared debug infrastructure in `src/debug/`, settings in
`src/config/` and cloud integration in `src/cloud/`.

CMake discovers implementations recursively, so moving a pair within `src/`
does not require a source list change. Update every consumer's include when
moving files, including standalone tools.

## Implementation boundaries

Prefer cohesive implementations around 150–400 lines. Review files above 500
lines for responsibilities that can be separated; a large settings table or a
cohesive codec may warrant more space. Split behavior into named classes with
matching headers and implementations. Keep existing entry points as small
delegating facades where callers share a stable interface.

| Facade / area | Implementation ownership |
| --- | --- |
| `core/assist/assist_controller` | `runtime/`: lifecycle, buffered firing, hold replanning and input dispatch; `input/`: input gates and hold policy; `contracts/`: timing rules; `planning/live/`: press, release and bake reuse; `reporting/`: overlay and observed outcomes |
| `core/sim/shadow_simulator` | `runtime/`: ghosts, section safety, player state and physics; `world/`: reversible capture and dynamic transforms; `evaluation/`: candidate runs, delay verification and frozen-state evaluation; `replay/`: replay/bake timelines; `validation/`: regression checks |
| `core/sim/planning/` | Shared plan types, candidate state, survival rules, plan policy and sequence planning. `search/` owns one search invocation; `search/probes/` owns input-window, threat and long-horizon probing; `search/selection/` owns choosing and publishing the result. |
| `core/bake/bake_map` | `generation/`: replay and brute-force generation; `windows/`: lookup, merging and refinement. The facade retains cache persistence and lifecycle. |
| `core/trigger_world/` | `baking/`: effect conversion and sliced bake jobs; `storage/`: cache codec; `validation/`: stored-world checks |
| `dev/capture/dev_trace_store` | `codec/`: snapshot, event and session JSON; `validation/`: trace-chain checks. The store retains recording lifecycle and disk writes. |
| `dev/evaluation/` | `attribution/`: causal attribution; `validation/`: rejection-ledger and fidelity checks |
| `dev/reporting/` | `diagnostics/`: ghost/hitbox, bake and state report sections; the dump facade handles output directories and assembling files |
| `ui/settings/` | `categories/`: key mapping and tab bar; `filtering/`: tree traversal and keeper lifecycle; `watchdog/`: popup discovery; import synchronization remains beside the public filter facade |
| `ui/cloud/` | `browser/`: layout, list/search/sort and selected-pack details; the popup retains network callbacks and user actions |
| `ui/pause/` | `actions/`: configuration and developer actions; the bridge retains common pause/menu dispatch |
| `cloud/` | `stats/`: local rating/download state and asynchronous tracking |
| `config/` | `cache/`: refreshing cached settings |
| `hooks/` | Separate layer-input, player, object and play-layer lifecycle hook pairs; a shared simulation guard supplies their common predicate |

`ShadowPlanContext` owns only per-invocation search state and references the
simulator. Search phases share that state without allocating callback wrappers.
`ShadowPlanSearch::run` retains the session guard across every phase and early
return, so splitting the search does not shorten capture/restore cleanup scope.
Planner policies, budgets, serialized keys and public result types retain their
existing contracts.

## Validation

Run `python tools/check_cpp_style.py`, then `geode build --config RelWithDebInfo`
and install the resulting package into the active Geode profile.

The recorded Wave input regression can also run without the game, using a C++23
compiler. For example, with MinGW from the repository root:

```powershell
g++ -std=c++23 tools/test_wave_input_window.cpp src/core/assist/contracts/miss_assist_contracts.cpp src/core/assist/contracts/assist_verdict.cpp -o build/test_wave_input_window.exe
./build/test_wave_input_window.exe
```
