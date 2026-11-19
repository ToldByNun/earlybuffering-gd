# Real Assist run matrix (APR2)

Smoke acceptance path before AP10 / Neural work. Physics runs inside Geometry Dash.

## Cases (Frozen protocol)

| id | variants | modes | notes |
| --- | --- | --- | --- |
| cataclysm | `on-time`, `pulse-shift-early-2` | Live, Hybrid | dual |
| quantum | `on-time`, `pulse-shift-early-2` | Live, Hybrid | dual |
| slaughterhouse | `on-time`, `pulse-shift-early-2` | Live, Hybrid | Wave / `wavePattern` |
| hoveropolis | `on-time`, `pulse-shift-early-2` | Live, Hybrid | Ship / `shipLine` |

Full smoke = **16** prep rows (4 × 2 × 2) under **Frozen**. Online is a separate ColdStart lane (not mixed into Frozen tables).

## Agent loop

```bat
python tools/sync_real_corpus.py --install-save
python tools/shift_replay.py --install-save
geode build
set EB_REAL_ASSIST_AUTO=matrix
geode run
```

Scrape:

- Geode console PASS/FAIL block
- `%LOCALAPPDATA%\GeometryDash\geode\mods\audesign.earlybuffer\real_assist\last_run.txt`
- `real_assist/session_*.json` after PlayLayer attempts

Single case:

```bat
set EB_REAL_ASSIST_AUTO=1
set EB_REAL_ASSIST_CASE=slaughterhouse/pulse-shift-early-2
set EB_REAL_ASSIST_MODE=Live
set EB_REAL_ASSIST_PROTOCOL=Frozen
geode run
```

## Manual Dev

Dev → Real Assist → **Run Real Assist Case** (uses `real-assist-case` / `real-assist-mode` / `real-assist-protocol` settings).

## Notes

- Prep-only matrix does **not** invent SurviveRate; open level + attempts fill session JSON.
- `PhysicsHitRate` / `InventRate` stay `n/a` until APR3 scorer + PhysicsSafeSet (`reproduce=pass` required).
- Frozen: Hybrid write-guarded (`freeze(true)`), Auto-Tune off. Online: reset + Auto-Tune on.
