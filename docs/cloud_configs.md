# Cloud configs

EarlyBuffer can browse a public catalog of portable settings packs (ConfigBundle v2 JSON) and install them in-game. Local Export / Import still work.

## Catalog repo

Default host: `ToldByNun/earlybuffer-configs`

- Primary CDN: `https://cdn.jsdelivr.net/gh/ToldByNun/earlybuffer-configs@main/index.json`
- Fallback: `https://raw.githubusercontent.com/ToldByNun/earlybuffer-configs/main/index.json`

## Layout

```
index.json
packs/<id>.json
```

### index.json

- `schema`: `1`
- `updatedAt`: ISO-8601 string
- `packs[]`:
  - `id`: `[a-z0-9-]{1,64}` (no leading/trailing `-`)
  - `name`, `author`, `description`, `tags[]`
  - `minModVersion`: Geode-style semver (`1.4.0` or `v1.4.0`)
  - `bundleVersion`: `1` or `2`
  - `sha256`: lowercase hex of the pack file bytes
  - `path`: must be exactly `packs/<id>.json`
  - `size`: byte length of the pack file (max 256 KiB)

### Pack file

Bare ConfigBundle document:

```json
{
  "version": 2,
  "settings": {
    "safe-mode": true,
    "assist-mode": "Live",
    "ticks-all": 4
  }
}
```

Missing keys are left unchanged on install. Cloud apply skips keybinds and `dev-*` keys.

## Local cache

`{mod saveDir}/cloud-configs/` — `index-cache.json`, `meta.json`, `active.json`, `packs/`, `outbox/`.

## Bundled seed

The mod ships `resources/cloud-seed/` for catalog mirroring, and compiles the same `safe-defaults` index/pack into the binary so offline browse works even if the package omits nested JSON resources.

## Adding a pack (maintainers)

1. Export settings in-game (or craft a v2 JSON).
2. Save as `packs/<id>.json`.
3. Compute SHA-256 of the file bytes and the exact size.
4. Append a row to `index.json`.
5. Publish to the catalog repo; jsDelivr may lag briefly behind GitHub.
