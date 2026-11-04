# eb-cloud-submit (Vercel)

Public endpoints for EarlyBuffer cloud configs. The GitHub token stays in Vercel Environment Variables — never in the mod package.

## Endpoints

| Path | Method | Role |
| --- | --- | --- |
| `/api/submit` (alias `/submit`) | POST | Publish pack + update `index.json` |
| `/api/stats` (alias `/stats`) | GET | Public counters only (`downloads`, `ratingSum`, `ratingCount`) |
| `/api/track` (alias `/track`) | POST | Record one download or one rating per `clientId` |

### Track body

```json
{
  "op": "download",
  "id": "safe-defaults",
  "clientId": "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
}
```

or

```json
{
  "op": "rate",
  "id": "safe-defaults",
  "clientId": "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
  "stars": 5
}
```

Rules:

- `clientId` required (`[a-zA-Z0-9-]{32,64}`)
- Pack `id` must exist in catalog `index.json`
- Same `clientId` + pack: **at most one download** and **at most one rating** (server stores hashed actor ids in private `stats.json` fields; GET never returns those lists)
- Re-rate is rejected (`409 already rated`)
- Soft IP limit ~10 track requests / minute / isolate

`stats.json` lives in `ToldByNun/earlybuffer-configs` (created on first track).

## Setup

1. Fine-grained PAT: only `earlybuffer-configs`, **Contents → Read and write**.
2. From this folder:

```bash
cd tools/cloud-submit-vercel
vercel login
vercel
vercel env add GITHUB_TOKEN
vercel --prod
```

Optional env: `REPO=ToldByNun/earlybuffer-configs`, `BRANCH=main`.

Production host used by EarlyBuffer: **`https://earlybuffer-config.vercel.app`**.

After adding stats/track, redeploy:

```bash
vercel --prod
```

## Quick tests

```bash
curl -sS https://earlybuffer-config.vercel.app/api/stats

curl -sS -X POST https://earlybuffer-config.vercel.app/api/track \
  -H "Content-Type: application/json" \
  -d "{\"op\":\"download\",\"id\":\"safe-defaults\",\"clientId\":\"11111111-1111-1111-1111-111111111111\"}"

curl -sS -X POST https://earlybuffer-config.vercel.app/api/track \
  -H "Content-Type: application/json" \
  -d "{\"op\":\"rate\",\"id\":\"safe-defaults\",\"clientId\":\"11111111-1111-1111-1111-111111111111\",\"stars\":5}"
```

Second download with the same `clientId` should return `"duplicate":true` without bumping the counter. Second rate should fail with `already rated`.
