# Real corpus (APR0+)

Gold level + clear pairs for the Real Assist Test Engine. Local binaries are **synced**, not hand-edited in git.

## Sync

```bat
python tools/sync_real_corpus.py
python tools/sync_real_corpus.py --install-save
```

- Source: `EB_REAL_CORPUS_SRC` or `C:\Users\mikaj\Music\earlybuffer`
- Fixture tree: `tools/fixtures/real_corpus/<id>/`
- `--install-save` also copies into the Geode mod save dir `…/audesign.earlybuffer/real_corpus/` for in-game runners (`EB_REAL_CORPUS_DIR` overrides)

Tracked: `manifest.json`, per-id `meta.json` / `reproduce.json`, this README.  
Ignored / local: `level.gmd`, `clear.gdr2`, `derived/` (shifts + oracles).

## Layout

```
real_corpus/
  manifest.json
  <id>/
    level.gmd
    clear.gdr2
    meta.json
    reproduce.json
  derived/          # APR1+ (gitignored)
```

Ids: `cataclysm`, `quantum`, `slaughterhouse`, `hoveropolis`.

## Gold reproduce gate

A clear is **untrusted for oracle construction** until `reproduce.json` has `"status": "pass"` from an unmodified PlayLayer run:

- Assist off; practice / startpos off unless tagged
- Same TPS / frame clock as import (`meta.clear.tps`)
- Unmodified `clear.gdr2` must finish; die or desync → `"status": "fail"`

Import sanity (offline):

```bat
python tools/test_real_corpus_import.py
```

In-game: Dev → **Reproduce Gold Clears**, or `geode run` with `EB_REAL_ASSIST_AUTO=1` (optional `EB_REAL_ASSIST_CASE=<id>`).

## APR1 — Shifts + dual oracles

```bat
python tools/shift_replay.py
python tools/test_shift_replay.py
python tools/build_verifier_safe.py --id cataclysm --family pulse-shift-early-2
python tools/build_physics_safe.py  --id cataclysm --family pulse-shift-early-2
```

- Derived variants land under `derived/<id>/` (gitignored) with `.gdr2` + `.sidecar.json`
- Families: `on-time`, `pulse-shift-*-k`, `edge-only-*-k` (k=1..8), `drop-click`
- Sidecar records shifted indices, hold lengths, and **pre_shifted_press** capture contract
- Oracle builders **refuse** unless `reproduce=pass`; offline they only emit `pending-probe` docs
- **PhysicsSafeSet** = acceptance ground truth; **VerifierSafeSet** = diagnostic only
