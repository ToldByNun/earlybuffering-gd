# Dev traces & reports

For debugging assist picks vs reality.

## Session JSON

Typical path (Windows):

`%LocalAppData%\GeometryDash\geode\mods\audesign.earlybuffer\devtraces\session-*.json`

Contains attempts → events (`pressObserved`, `planFinished`, `gatePass`, …) with:

- `assistReason`, `plannedDelay`
- `candNotes` (chosen / rejected / skipped + reject codes)
- `witness` (player blob, trigger ready tracks, clock / midStep)
- `attribution` (e.g. `best-surviving-absent`)

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

Empty `skip=not-viable` without a named cut usually means unevaluated leftovers — file a bug if you still see that after 1.5.1.

`planFinished` may also carry hitch/Adaptive cost bits: `planUs`, `budgetHit`, `candidatesTried`, `adaptiveShell`, `adaptiveFellThrough`.

## Lag / perf

| Tool | Output idea |
|---|---|
| Lag Probe | `earlybuffer-lagprobe.log` death/restart episodes |
| Perf Trace | per-tick timing / `earlybuffer-perf.log` |
| Assist debug log | planner / SAVED lines in Geode console |

## Resource stats

Newer builds can attach RAM/CPU-ish stats to sessions/reports (if present in that package). Older `v1.5.0` traces may lack them.

## Code owners

`src/dev/capture/`, `src/dev/evaluation/`, `src/dev/reporting/`.
