# Source architecture

`src/` groups paired C++ headers and implementations by responsibility. Split a
folder when it contains several distinct responsibilities or becomes difficult
to scan. Keep cohesive small modules together; do not add a directory for every
class. The current leaf folders contain at most six implementation/header pairs.
Keep new files with the behavior they belong to, and revisit the split as the
project grows. File count is a review signal, not a mandatory numeric limit.

The root entry pair delegates module initialization. `config/` and `hooks/` each
contain one pair, and `debug/` contains four shared diagnostic pairs, so they
remain flat. The larger areas are organized as follows:

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

## Validation

Run `python tools/check_cpp_style.py`, then `geode build --config RelWithDebInfo`
and install the resulting package into the active Geode profile.

The recorded Wave input regression can also run without the game, using a C++23
compiler. For example, with MinGW from the repository root:

```powershell
g++ -std=c++23 tools/test_wave_input_window.cpp src/core/assist/contracts/miss_assist_contracts.cpp src/core/assist/contracts/assist_verdict.cpp -o build/test_wave_input_window.exe
./build/test_wave_input_window.exe
```
