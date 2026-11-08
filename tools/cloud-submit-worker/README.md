# eb-cloud-submit Worker

This service supports Cloud Configs in EarlyBuffer **v1.4.3**. The in-game entry is Settings → Assist → Assist Actions → Cloud Configs, below Import Config. See [the cloud system documentation](../../docs/cloud_configs.md) for the currently configured endpoints.

Public Share endpoint for EarlyBuffer. **GitHub token stays in Cloudflare Secrets** — never in the mod package.

## Why

Anyone can Share from the game. The client only POSTs JSON to this Worker. The Worker validates + rate-limits, then uses a server-side PAT to update `ToldByNun/earlybuffer-configs`.

## One-time setup

1. **Revoke** any PAT that was pasted into chat or considered for shipping in the client.
2. Create a **new** fine-grained PAT: repo `earlybuffer-configs` only, **Contents: Read and write**.
3. Install Wrangler: `npm i -g wrangler` (or use `npx`).
4. Login: `npx wrangler login`
5. From this folder:

```bash
npx wrangler secret put GITHUB_TOKEN
# paste the new PAT

npx wrangler deploy
```

6. Note the URL Wrangler prints, e.g. `https://eb-cloud-submit.<account>.workers.dev`.
   - If the hostname is **not** `eb-cloud-submit.toldbynun.workers.dev`, set in-game **Cloud Submit URL** to `https://YOUR-HOST/submit`, **or** rename the Worker in `wrangler.toml` / use a route that matches the built-in default.

7. Optional KV rate-limit: create a KV namespace, bind as `RATE` in `wrangler.toml` (see Cloudflare docs). Without KV, validation still runs but IP rate-limit is skipped.

## Test

```bash
curl -sS -X POST https://eb-cloud-submit.toldbynun.workers.dev/submit \
  -H "Content-Type: application/json" \
  -d "{\"schema\":1,\"id\":\"curl-test\",\"name\":\"Curl Test\",\"author\":\"you\",\"description\":\"\",\"tags\":[\"shared\"],\"minModVersion\":\"1.4.1\",\"bundleVersion\":2,\"bundle\":{\"version\":2,\"settings\":{\"safe-mode\":true}}}"
```

Then open the catalog repo — `packs/curl-test.json` + `index.json` should update. In-game **Refresh** after ~1 minute (CDN lag).
