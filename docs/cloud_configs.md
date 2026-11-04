# Cloud configs

EarlyBuffer browses the public catalog **`ToldByNun/earlybuffer-configs`** and can Share packs into it. There are **no player-facing cloud URL/token settings** — wiring is built into the mod.

## In-game

Assist → **Cloud Configs**: search / detail / Install. **Share…** writes a local outbox and POSTs to the built-in submit endpoint.

## Fixed endpoints

| Role | URL |
| --- | --- |
| Catalog (primary) | `https://cdn.jsdelivr.net/gh/ToldByNun/earlybuffer-configs@main/index.json` |
| Catalog (fallback) | `https://raw.githubusercontent.com/ToldByNun/earlybuffer-configs/main/index.json` |
| Share submit | `https://earlybuffer-config.vercel.app/api/submit` |

The Vercel function holds `GITHUB_TOKEN` as a server secret and updates the catalog repo. Deploy notes: [`tools/cloud-submit-vercel/README.md`](../tools/cloud-submit-vercel/README.md).

## Layout

```
index.json
packs/<id>.json
```

Pack files are bare ConfigBundle v2 JSON. Cloud apply skips keybinds and `dev-*` keys.

## Local cache

`{mod saveDir}/cloud-configs/` — index cache, packs, outbox. Refresh cache-busts the network fetch.

## Bundled seed

`resources/cloud-seed/` (`safe-defaults`) for offline browse when the network is down.
