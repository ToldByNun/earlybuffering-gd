# Real Assist baseline (APR4)

Gate before **AP00** feature work. Acceptance metric is **PhysicsHitRate**; **VerifierHitRate** is diagnostic only.

- Frozen at: `2026-11-19T23:00:39Z`
- Protocol: **Frozen** only (Online is a separate ColdStart lane)
- Fixtures: synthetic until each corpus `reproduce.json` is `pass` and PlayLayer fills real PhysicsSafeSet bands

## Dual honesty

Dual corpora (cataclysm, quantum, hoveropolis) must be graded Live vs Hybrid fairly; no Hybrid/AI-first shortcut.

## Labeling: pulse-shift vs edge-only

- Pulse-shift: pulse-shift-early-2 shifts the whole hold (press+release).
- Edge-only: edge-only-early-2 shifts press edge only - never alias as pulse-shift.
- Smoke subset uses `pulse-shift-early-2` (and `quantum/on-time`) - never alias edge-only.

## Smoke keys

- `cataclysm/pulse-shift-early-2`
- `hoveropolis/pulse-shift-early-2`
- `slaughterhouse/pulse-shift-early-2`
- `quantum/on-time`

## Live vs Hybrid (Frozen)

| corpus | variant | Live Survive | Live PhysicsHit | Hybrid Survive | Hybrid PhysicsHit | d PhysicsHit |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| cataclysm | pulse-shift-early-2 | 1.0 | 1.0 | 1.0 | 1.0 | 0.0 |
| hoveropolis | pulse-shift-early-2 | 1.0 | 1.0 | 1.0 | 1.0 | 0.0 |
| quantum | on-time | 1.0 | 1.0 | 1.0 | 1.0 | 0.0 |
| slaughterhouse | pulse-shift-early-2 | 1.0 | 1.0 | 1.0 | 1.0 | 0.0 |

## Gate commands

```bat
python tools/test_real_assist_score.py
python tools/test_real_assist_smoke.py
python tools/test_real_assist_smoke.py --freeze
python tools/run_live_regression_ap07.py
```

Hard-fail: `InventRate > 0`, on-time SurviveRate below frozen floor, missing smoke oracle, or Physics/Verifier claim/kind mismatch.

## Dense coverage (not just smoke)

Smoke (4 keys) is the AP00 gate only. Assist quality needs multi-click x shift x delay-band coverage:

```bat
python tools/test_real_assist_dense.py --freeze
python tools/test_real_assist_dense.py
```

Default: `quantile:8` clicks/level, sequences `1,2,3`, tick windows `4,8,12,24`,
families x k, delay `0..limit` per window. Includes consecutive multi-click shifts (not
only single clicks). Real PlayLayer bands replace synthetics after `reproduce=pass`.

## AP00 rule

Do not start AP00 (AI mode / neural scaffolding) until this baseline file exists and `python tools/test_real_assist_smoke.py` is green. Keep the dense gate green for meaningful assist results.
