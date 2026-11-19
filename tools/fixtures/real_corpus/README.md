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
