# Dev traces & reports

For debugging assist picks vs reality.

## Session JSON

Typical path (Windows):

`%LocalAppData%\GeometryDash\geode\mods\audesign.earlybuffer\devtraces\session-*.json`

Contains attempts → events (`pressObserved`, `planFinished`, `gatePass`, `modeTransition`, …) with:

- `assistReason`, `plannedDelay`
- `candNotes` (chosen / rejected / skipped + reject codes; when evaluated also `deathTick` / `ticksRan` / `unknown` / `budgetHit` / `surviveTicks` / `survivalClass`)
- `plannerPath` + `gameModeKind` + `modeParams` + hold/threat honesty on `planFinished`
- attempt-scoped `modeTransition` / `checkpoint` timeline (`timelineChange`, mode/speed/gravity/dual flags)
- `witness` (player blob, trigger ready tracks, clock / midStep)
- `attribution` (e.g. `best-surviving-absent`)

After **Eval Last / Eval Session / Eval On Death**, each evaluated attempt also gets an additive `eval` object:

- fidelity / claims / objective / grid / horizon / tail
- `bestSurvivingDelay` / `bestEvaluatedDelay` (when claims allow)
- `samples[]` with `delay`, `survivedPrimary`, `deathTick`, `endX`, `unknown`, `budgetHit`, tail fields, `ticksRan`
- `overlay` line for that seal

Research Pack copies this sealed session as `session.json`.

## Report line

`earlybuffer-dev-report.txt` style:

```text
REC 5/10 | fid=reproduced | bestSurv=5 | bestEval=5 obj=survive_then_endx_v1 | …
```

| Field | Meaning |
|---|---|
| `bestSurv` | Offline best **surviving** delay under the eval objective |
| `bestEval` | Best evaluated delay (may be a least-bad death) |
| `obj` | Usually `survive_then_endx_v1` |
| `harm` / `net` | Counterfactual estimate vs live arm |

If Live armed `d=2` but `bestSurv=5`, check whether Live **skipped** later delays. Honest skip codes:

| `rejectCode` | Meaning |
|---|---|
| `budget-not-reached` | Wall/tick budget cut the search |
| `prefer-band-early-exit` | Bake prefer ±k done |
| `same-crash-cut` | Same deathTick across delays → stop |
| `earliest-safe-exit` | Continuous input-window took first safe |
| `peak-death-cut` | Discrete input-window stopped after survivor then acted death (AP1 wires honest skips) |
| `not-viable` | Really rejected **or** legacy opaque skip |

Empty `skip=not-viable` without a named cut usually means unevaluated leftovers — file a bug if you still see that after 1.5.1 / 1.6.0.

`planFinished` may also carry hitch/Adaptive cost bits: `planUs`, `budgetHit`, `candidatesTried`, `adaptiveShell`, `adaptiveFellThrough`.

Planner-branch honesty (AP4):

| Field | Meaning |
|---|---|
| `plannerPath` | `inputWindow` / `threatProbe` / `adaptive` / `classic` / `hybridFallthrough` |
| `gameModeKind` | Resolved mode id at plan time (`cube`…`swing`) |
| `modeParams` | ticks / cap / λ / Adaptive quanta for that mode |
| `viableDelays` / hold / `threatZone` | Plan honesty for Continuous / Wave research |
| `candNotes[].survivalClass` | `alive` / `landing` / `recoverable` / `late-crash` / `lethal` |

Mode-transition timeline (AP5; `pressId=0`, not part of a press chain):

| `kind` / `timelineChange` | Meaning |
|---|---|
| `modeTransition` / `seed` | First snapshot when recording sees P1 |
| `modeTransition` / `mode` | Gamemode portal change (`fromGameModeKind` → `gameModeKind`) |
| `modeTransition` / `speed` | Speed portal (`playerSpeed`) |
| `modeTransition` / `gravity` | Gravity flip (`upsideDown`) |
| `modeTransition` / `dual` | Dual on/off |
| `checkpoint` / `checkpoint` | Practice checkpoint load (tick reset) |

## Lag / perf

| Tool | Output idea |
|---|---|
| Lag Probe | `earlybuffer-lagprobe.log` death/restart episodes |
| Perf Trace | per-tick timing / `earlybuffer-perf.log` |
| Assist debug log | planner / SAVED lines in Geode console |

## Resource stats

Newer builds can attach RAM/CPU-ish stats to sessions/reports (if present in that package). Older `v1.5.0` traces may lack them.

## Research pack

Dev tab **Export Research Pack** (or `dev-export-research-pack-action`) is one click:
runs **Eval Session** (fallback Eval Last), then writes:

`%LocalAppData%\GeometryDash\geode\mods\audesign.earlybuffer\research\research_<stamp>\`

Clipboard gets the **pack folder path** (send that folder).

| File | Contents |
|---|---|
| `MANIFEST.json` | Schema, session/level ids, file list |
| `session.json` | Open session or newest `devtraces/session-*.json` |
| `report.txt` / `report.json` | Last eval overlay + structured `jsonLine` |
| `settings_fingerprint.txt` | Compact assist/dev fingerprint |
| `config.json` | Portable settings, keybinds omitted |
| `README.txt` | Pack legend |

Bake bugs still use **Dump All Diag** (`diag/full_*`), not the research pack.

## Code owners

`src/dev/capture/`, `src/dev/evaluation/`, `src/dev/reporting/`.
