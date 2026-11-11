# Source layout

Short map. Full rules: [Source architecture](/source_architecture).

## Top level

| Path | Role |
|---|---|
| `src/early_buffer_module.cpp` | Geode entry — keep thin |
| `src/config/` | `StageConfig` cache / settings facade |
| `src/core/assist/` | Controller, planning, contracts, input buffer |
| `src/core/sim/` | Shadow simulator, survival, Adaptive search |
| `src/core/bake/` | Bake maps, verify, hybrid cache, self-bake |
| `src/core/trigger_world/` | Trigger classify / bake / store |
| `src/core/bundle/` | Config bundle codec |
| `src/hooks/` | `$modify` hooks |
| `src/ui/` | Settings, pause, overlays, cloud UI |
| `src/dev/` | Capture, evaluation, reports |
| `src/debug/` | Assist log, LagProbe, PerfTrace, PlanGold |
| `src/cloud/` | Catalog, transport, sharing |
| `mod.json` | Mod metadata + settings schema |

## Coding rules (project)

From `AGENTS.md`:

- Paired `snake_case.hpp` / `.cpp` with include guards
- No project namespaces / `using namespace`
- Early returns for invalid / done cases
- Classes for behavior + state; tiny entry macros
- Format with `.clang-format`; `python tools/check_cpp_style.py`

## Where to change what

| Goal | First folders |
|---|---|
| Live Delay pick | `core/sim/planning/search/`, `assist/planning/` |
| Adaptive | `core/sim/planning/search/adaptive/` |
| Input-window / ball landing | `probes/shadow_plan_input_window.cpp` |
| Settings UI labels | `mod.json` + `StageConfig` |
| Session JSON / report line | `src/dev/` |
| Overlay text | `src/ui/overlays/` |

## Ghosts vs other mods

Shadow / snapshot / eval bodies are real `PlayerObject`s on `objectLayer` (needed for collision). They are **not** `m_player1` / `m_player2`.

Markers other mods can use to ignore them:

- Node id prefix: `earlybuffer-shadow-`, `earlybuffer-snapshot-`, `earlybuffer-eval-`
- User object key: `audesign.earlybuffer/sim-ghost` (CCBool true)
- Runtime: `EbShadowSimulator::isOwnedGhost(player)`

Sim death is `simulator.m_dead` — ghosts must not stay `m_isDead` (breaks Mega Hack / QOL “hitboxes on death”).
Mega Hack / OpenHack may latch an internal `s_isDead` on **any** `destroyPlayer` (even swallowed); EarlyBuffer sets `suppressDeathHitboxes` and force-hides `m_debugDrawNode` in `postUpdate` while real P1 is alive.
