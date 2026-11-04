# eb-cloud-submit (Vercel)

Same idea as the Cloudflare Worker: **no server of your own**, free Vercel hobby plan is enough. The GitHub token stays in Vercel Environment Variables — never in the EarlyBuffer mod package.

## One-time setup (~5 min)

1. **Revoke** any PAT that was pasted into chat.
2. New fine-grained PAT: only `ToldByNun/earlybuffer-configs`, **Contents → Read and write**.
3. Install CLI (once): `npm i -g vercel`
4. From this folder:

```bash
cd tools/cloud-submit-vercel
vercel login
vercel
# follow prompts: new project, accept defaults
```

5. Set the secret (Production + Preview):

```bash
vercel env add GITHUB_TOKEN
# paste PAT, choose Production (and Preview if you want)
vercel --prod
```

Optional env (defaults are fine):
- `REPO=ToldByNun/earlybuffer-configs`
- `BRANCH=main`

6. Production URL for EarlyBuffer: **`https://earlybuffer-config.vercel.app`**.
   Submit endpoint: **`https://earlybuffer-config.vercel.app/api/submit`**  
   (alias `/submit` also works via `vercel.json`). For a fresh project, Vercel may print
   `https://YOUR-PROJECT.vercel.app` instead — same path `/api/submit`.

7. In Geometry Dash → EarlyBuffer → Assist:
   - **Cloud Public Submit** = ON  
   - **Cloud Submit URL** = empty (uses built-in `https://earlybuffer-config.vercel.app/api/submit`) or set that URL explicitly

8. Share… in-game → check the GitHub repo → Refresh after ~1 minute.

## Test

```bash
curl -sS -X POST https://earlybuffer-config.vercel.app/api/submit \
  -H "Content-Type: application/json" \
  -d "{\"schema\":1,\"id\":\"curl-test\",\"name\":\"Curl Test\",\"author\":\"you\",\"description\":\"\",\"tags\":[\"shared\"],\"minModVersion\":\"1.4.1\",\"bundleVersion\":2,\"bundle\":{\"version\":2,\"settings\":{\"safe-mode\":true}}}"
```

## Notes

- Cloudflare Worker (`../cloud-submit-worker`) is equivalent — pick **one**.
- Hobby Vercel is fine for catalog submits; add rate-limiting later if spam appears.
