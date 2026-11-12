# Cloud configs

Release **v1.6.0** includes this system (shipped since v1.4.3). Open **EarlyBuffer Settings → Learn → Config → Cloud Configs** (desktop menu) or classic Assist actions — both open the Geode popup in 1.6.0. Restart GD after installing a new package to load its settings definitions.

EarlyBuffer browses the public catalog **`ToldByNun/earlybuffer-configs`** and can Share packs into it. There are **no player-facing cloud URL/token settings** — catalog, submit, and stats URLs are built into the mod. The GitHub token lives only in Vercel env vars.

## In-game

Assist → **Cloud Configs**:

| Control | Behavior |
| --- | --- |
| Search | Filters the catalog by name / author / id / tags |
| **Sort:** | Cycles Default → Downloads → Rating → Popular → Name (composes with search). Popular = `downloads + ratingCount×5 + avg×20` |
| **Refresh** | Cache-busting catalog + stats fetch; button sits inset from the top-right chrome |
| List / detail | Download counts and average rating when `/api/stats` is reachable |
| Stars (1–5) | One vote per anonymous `clientId`; re-rate replaces stars (count unchanged) |
| **Install** | Applies pack (skips keybinds + `dev-*`); records one download per `clientId` |
| **Share…** | Name\|Author, description, tags (comma/space; empty → `shared`) → local outbox + POST submit |

Offline: bundled seed pack `safe-defaults` still appears when the network is down.

## Fixed endpoints

| Role | URL |
| --- | --- |
| Catalog (primary) | `https://cdn.jsdelivr.net/gh/ToldByNun/earlybuffer-configs@main/index.json` |
| Catalog (fallback) | `https://raw.githubusercontent.com/ToldByNun/earlybuffer-configs/main/index.json` |
| Share submit | `https://earlybuffer-config.vercel.app/api/submit` |
| Stats (GET) | `https://earlybuffer-config.vercel.app/api/stats` |
| Track (POST) | `https://earlybuffer-config.vercel.app/api/track` |

Deploy notes and curl probes: [`tools/cloud-submit-vercel/README.md`](../tools/cloud-submit-vercel/README.md).

## Repo layout (`ToldByNun/earlybuffer-configs`)

```
index.json          # catalog entries
packs/<id>.json     # bare ConfigBundle v2
stats.json          # downloads / ratingSum / ratingCount + hashed actor sets
```

Public `/api/stats` strips hashes and returns only counters. `/api/track` updates `stats.json` with CAS against GitHub Contents.

## Local cache

`{mod saveDir}/cloud-configs/`:

| Path | Role |
| --- | --- |
| `index-cache.json`, `meta.json`, `active.json` | Catalog / apply state |
| `packs/<id>.json` | Downloaded packs |
| `outbox/` | Share envelopes awaiting / after submit |
| `client-id.txt` | Persistent anonymous UUID (`[a-zA-Z0-9-]{32,64}`) |
| `local-stats.json` | Already-downloaded / rated ids + local `myStars` |

Track always sends `clientId`. Local sets skip redundant POSTs (same download; same star value). Re-rate with a new star value is allowed.

## Maintainer smoke (after RelWithDebInfo package)

1. Deploy API if needed: `cd tools/cloud-submit-vercel && vercel --prod`. Confirm `GET https://earlybuffer-config.vercel.app/api/stats` returns JSON (not 404).
2. Install the smoke `.geode` (`audesign.earlybuffer.geode` from `geode package new`, or `build/audesign.earlybuffer.geode`).
3. Boot GD → Assist → Cloud Configs: catalog loads (or seed offline); **Refresh** clears title/close; **Sort** cycles; badges show when stats exist.
4. Install a pack → download count bumps once; second install with same save does not bump.
5. Rate stars → average updates; change stars → count stays, average moves.
6. Share… with tags → outbox file + submit 200 (or clear error); no token fields in settings.
7. Dev Copy Report / PlanGold: `cloud-config-stats` (and other cloud-* checks) OK.

## Bundled seed

`resources/cloud-seed/` (`safe-defaults`) for offline browse when the network is down.
