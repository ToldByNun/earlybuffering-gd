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

Single case (prep only — stays on menu):

```bat
set EB_REAL_ASSIST_AUTO=1
set EB_REAL_ASSIST_CASE=slaughterhouse/pulse-shift-early-2
set EB_REAL_ASSIST_MODE=Live
set EB_REAL_ASSIST_PROTOCOL=Frozen
geode run
```

Opt-in level enter (normally off):

```bat
set EB_REAL_ASSIST_OPEN_LEVEL=1
```

## Manual Dev

Dev → Real Assist → **Run Real Assist Case** (uses `real-assist-case` / `real-assist-mode` / `real-assist-protocol` settings).

## Notes

- Prep-only matrix does **not** invent SurviveRate; open level + attempts fill session JSON.
- Offline score: `python tools/score_session.py <session.json>` (PhysicsHitRate acceptance)
- Mode compare: `python tools/compare_modes.py <sessions…>` — Frozen vs Online stay separate
- CI goldens: `python tools/test_real_assist_score.py`
- Frozen: Hybrid write-guarded (`freeze(true)`), Auto-Tune off. Online: reset + Auto-Tune on.
