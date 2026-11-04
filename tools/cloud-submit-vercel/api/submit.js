/**
 * EarlyBuffer public cloud-config submit (Vercel Serverless).
 * Client POSTs pack JSON (no secret). GITHUB_TOKEN lives in Vercel env only.
 *
 * Deploy: see ../README.md
 */

const ID_RE = /^[a-z0-9]([a-z0-9-]{0,62}[a-z0-9])?$/;
const MAX_BYTES = 256 * 1024;
const REPO = process.env.REPO || "ToldByNun/earlybuffer-configs";
const BRANCH = process.env.BRANCH || "main";

module.exports = async function handler(req, res) {
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type, Accept");

  if (req.method === "OPTIONS") {
    return res.status(204).end();
  }
  if (req.method !== "POST") {
    return res.status(405).json({ error: "POST only" });
  }

  const token = process.env.GITHUB_TOKEN;
  if (!token) {
    return res.status(503).json({ error: "server misconfigured (no token)" });
  }

  const body = typeof req.body === "string" ? safeJson(req.body) : req.body;
  if (!body) {
    return res.status(400).json({ error: "invalid json" });
  }

  const check = validateBody(body);
  if (check.error) {
    return res.status(400).json({ error: check.error });
  }

  try {
    const result = await publishToRepo(token, check.meta, check.bundleText);
    return res.status(200).json({ ok: true, url: result.url, id: check.meta.id });
  } catch (e) {
    const msg = e && e.message ? e.message : "publish failed";
    return res.status(502).json({ error: msg });
  }
};

function safeJson(s) {
  try {
    return JSON.parse(s);
  } catch {
    return null;
  }
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
  if (body.bundle.version !== 1 && body.bundle.version !== 2) {
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

async function sha256Hex(text) {
  const { createHash } = await import("node:crypto");
  return createHash("sha256").update(text, "utf8").digest("hex");
}

function b64(text) {
  return Buffer.from(text, "utf8").toString("base64");
}

async function gh(token, method, path, body) {
  const res = await fetch(`https://api.github.com${path}`, {
    method,
    headers: {
      Accept: "application/vnd.github+json",
      Authorization: `Bearer ${token}`,
      "X-GitHub-Api-Version": "2022-11-28",
      "User-Agent": "eb-cloud-submit-vercel",
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
    throw new Error((json && json.message) || text || `http ${res.status}`);
  }
  return json;
}

async function getContent(token, path) {
  try {
    return await gh(
      token,
      "GET",
      `/repos/${REPO}/contents/${path}?ref=${BRANCH}`
    );
  } catch (e) {
    if (String(e.message).includes("Not Found")) return null;
    throw e;
  }
}

async function putContent(token, path, message, contentText, sha) {
  const body = {
    message,
    content: b64(contentText),
    branch: BRANCH,
  };
  if (sha) body.sha = sha;
  return gh(token, "PUT", `/repos/${REPO}/contents/${path}`, body);
}

async function publishToRepo(token, meta, bundleText) {
  const packPath = `packs/${meta.id}.json`;
  const sha256 = await sha256Hex(bundleText);
  const size = Buffer.byteLength(bundleText, "utf8");

  const existingPack = await getContent(token, packPath);
  const packPut = await putContent(
    token,
    packPath,
    `cloud-submit: pack ${meta.id}`,
    bundleText,
    existingPack && existingPack.sha
  );

  let indexObj = { schema: 1, updatedAt: new Date().toISOString(), packs: [] };
  let indexSha = null;
  const existingIndex = await getContent(token, "index.json");
  if (existingIndex && existingIndex.content) {
    indexSha = existingIndex.sha;
    const raw = Buffer.from(
      String(existingIndex.content).replace(/\n/g, ""),
      "base64"
    ).toString("utf8");
    try {
      indexObj = JSON.parse(raw);
    } catch {
      /* reset */
    }
  }
  if (!indexObj || typeof indexObj !== "object") {
    indexObj = { schema: 1, packs: [] };
  }
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

  const indexText = `${JSON.stringify(indexObj, null, 2)}\n`;
  await putContent(
    token,
    "index.json",
    `cloud-submit: index ${meta.id}`,
    indexText,
    indexSha
  );

  const url =
    (packPut && packPut.content && packPut.content.html_url) ||
    (packPut && packPut.commit && packPut.commit.html_url) ||
    `https://github.com/${REPO}/blob/${BRANCH}/${packPath}`;
  return { url };
}
