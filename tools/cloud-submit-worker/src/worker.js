/**
 * EarlyBuffer public cloud-config submit Worker.
 * Client POSTs pack JSON (no secret). This Worker holds GITHUB_TOKEN and
 * upserts packs/<id>.json + index.json on the catalog repo.
 *
 * Deploy:
 *   cd tools/cloud-submit-worker
 *   npx wrangler secret put GITHUB_TOKEN
 *   npx wrangler deploy
 *
 * Revoke any PAT that was ever pasted into chat / shipped in a client.
 */

const ID_RE = /^[a-z0-9]([a-z0-9-]{0,62}[a-z0-9])?$/;
const MAX_BYTES = 256 * 1024;

export default {
  async fetch(request, env, ctx) {
    if (request.method === "OPTIONS") {
      return cors(new Response(null, { status: 204 }));
    }
    const url = new URL(request.url);
    if (request.method === "GET" && url.pathname === "/") {
      return cors(json({ ok: true, service: "eb-cloud-submit" }));
    }
    if (request.method !== "POST" || url.pathname !== "/submit") {
      return cors(json({ error: "not found" }, 404));
    }

    const ip = request.headers.get("CF-Connecting-IP") || "unknown";
    const limited = await rateLimit(env, ip, Number(env.MAX_PER_HOUR || 5));
    if (limited) {
      return cors(json({ error: "rate limited — try later" }, 429));
    }

    let body;
    try {
      body = await request.json();
    } catch {
      return cors(json({ error: "invalid json" }, 400));
    }

    const check = validateBody(body);
    if (check.error) return cors(json({ error: check.error }, 400));

    const token = env.GITHUB_TOKEN;
    if (!token) {
      return cors(json({ error: "server misconfigured (no token)" }, 503));
    }

    const repo = env.REPO || "ToldByNun/earlybuffer-configs";
    const branch = env.BRANCH || "main";

    try {
      const result = await publishToRepo(token, repo, branch, check.meta, check.bundleText);
      return cors(json({ ok: true, url: result.url, id: check.meta.id }));
    } catch (e) {
      const msg = e && e.message ? e.message : "publish failed";
      return cors(json({ error: msg }, 502));
    }
  },
};

function cors(res) {
  const headers = new Headers(res.headers);
  headers.set("Access-Control-Allow-Origin", "*");
  headers.set("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
  headers.set("Access-Control-Allow-Headers", "Content-Type, Accept");
  return new Response(res.body, { status: res.status, headers });
}

function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

function validateBody(body) {
  if (!body || typeof body !== "object") return { error: "body required" };
  if (body.schema !== 1) return { error: "schema must be 1" };
  const id = String(body.id || "");
  if (!ID_RE.test(id)) return { error: "invalid id" };
  const name = String(body.name || "").trim();
  if (!name || name.length > 64) return { error: "invalid name" };
  const author = String(body.author || "").slice(0, 64);
  const description = String(body.description || "").slice(0, 400);
  const tags = Array.isArray(body.tags)
    ? body.tags.map((t) => String(t).slice(0, 24)).filter(Boolean).slice(0, 8)
    : ["shared"];
  const minModVersion = String(body.minModVersion || "1.4.1").slice(0, 32);
  const bundleVersion = Number(body.bundleVersion || 2);
  if (bundleVersion !== 1 && bundleVersion !== 2) return { error: "bundleVersion" };
  if (!body.bundle || typeof body.bundle !== "object") return { error: "bundle required" };
  if (body.bundle.version !== 2 && body.bundle.version !== 1) {
    return { error: "bundle.version must be 1 or 2" };
  }
  if (!body.bundle.settings || typeof body.bundle.settings !== "object") {
    return { error: "bundle.settings required" };
  }
  const bundleText = JSON.stringify(body.bundle);
  if (bundleText.length > MAX_BYTES) return { error: "bundle too large" };

  return {
    meta: {
      id,
      name,
      author,
      description,
      tags: tags.length ? tags : ["shared"],
      minModVersion,
      bundleVersion,
    },
    bundleText,
  };
}

async function rateLimit(env, ip, maxPerHour) {
  if (!env.RATE) return false; // no KV bound → skip (still validate packs)
  const key = `rl:${ip}:${new Date().toISOString().slice(0, 13)}`;
  const cur = Number((await env.RATE.get(key)) || "0");
  if (cur >= maxPerHour) return true;
  await env.RATE.put(key, String(cur + 1), { expirationTtl: 3700 });
  return false;
}

async function sha256Hex(text) {
  const data = new TextEncoder().encode(text);
  const digest = await crypto.subtle.digest("SHA-256", data);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

function b64(text) {
  // Workers: btoa only for latin1 — use TextEncoder path
  const bytes = new TextEncoder().encode(text);
  let s = "";
  for (const b of bytes) s += String.fromCharCode(b);
  return btoa(s);
}

async function gh(token, method, path, body) {
  const res = await fetch(`https://api.github.com${path}`, {
    method,
    headers: {
      Accept: "application/vnd.github+json",
      Authorization: `Bearer ${token}`,
      "X-GitHub-Api-Version": "2022-11-28",
      "User-Agent": "eb-cloud-submit-worker",
      ...(body ? { "Content-Type": "application/json" } : {}),
    },
    body: body ? JSON.stringify(body) : undefined,
  });
  const text = await res.text();
  let json = null;
  try {
    json = text ? JSON.parse(text) : null;
  } catch {
    json = null;
  }
  if (!res.ok) {
    const msg = (json && json.message) || text || `http ${res.status}`;
    throw new Error(msg);
  }
  return json;
}

async function getContent(token, repo, path, branch) {
  const q = encodeURIComponent(path);
  try {
    return await gh(token, "GET", `/repos/${repo}/contents/${q}?ref=${branch}`);
  } catch (e) {
    if (String(e.message).includes("Not Found")) return null;
    throw e;
  }
}

async function putContent(token, repo, path, branch, message, contentText, sha) {
  const body = {
    message,
    content: b64(contentText),
    branch,
  };
  if (sha) body.sha = sha;
  return gh(token, "PUT", `/repos/${repo}/contents/${encodeURIComponent(path).replace(/%2F/g, "/")}`, body);
}

async function publishToRepo(token, repo, branch, meta, bundleText) {
  const packPath = `packs/${meta.id}.json`;
  const sha256 = await sha256Hex(bundleText);
  const size = new TextEncoder().encode(bundleText).length;

  const existingPack = await getContent(token, repo, packPath, branch);
  const packPut = await putContent(
    token,
    repo,
    packPath,
    branch,
    `cloud-submit: pack ${meta.id}`,
    bundleText,
    existingPack && existingPack.sha
  );

  let indexObj = { schema: 1, updatedAt: new Date().toISOString(), packs: [] };
  let indexSha = null;
  const existingIndex = await getContent(token, repo, "index.json", branch);
  if (existingIndex && existingIndex.content) {
    indexSha = existingIndex.sha;
    const raw = atob(String(existingIndex.content).replace(/\n/g, ""));
    try {
      indexObj = JSON.parse(raw);
    } catch {
      /* reset */
    }
  }
  if (!indexObj || typeof indexObj !== "object") indexObj = { schema: 1, packs: [] };
  if (!Array.isArray(indexObj.packs)) indexObj.packs = [];

  const entry = {
    id: meta.id,
    name: meta.name,
    author: meta.author,
    description: meta.description,
    tags: meta.tags,
    minModVersion: meta.minModVersion,
    bundleVersion: meta.bundleVersion,
    sha256,
    path: packPath,
    size,
  };
  const idx = indexObj.packs.findIndex((p) => p && p.id === meta.id);
  if (idx >= 0) indexObj.packs[idx] = entry;
  else indexObj.packs.push(entry);
  indexObj.schema = 1;
  indexObj.updatedAt = new Date().toISOString();

  // Stable-ish stringify (no prettier) — size/sha of packs are independent.
  const indexText = JSON.stringify(indexObj, null, 2) + "\n";
  await putContent(
    token,
    repo,
    "index.json",
    branch,
    `cloud-submit: index ${meta.id}`,
    indexText,
    indexSha
  );

  const url =
    (packPut && packPut.content && packPut.content.html_url) ||
    (packPut && packPut.commit && packPut.commit.html_url) ||
    `https://github.com/${repo}/blob/${branch}/${packPath}`;
  return { url };
}
