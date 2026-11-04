/**
 * POST download / rate with 1 actor = 1 download and 1 actor = 1 rate.
 * Body: { op: "download"|"rate", id, clientId, stars? }
 */

const { isRateLimited } = require("../lib/rateLimit");
const {
  ID_RE,
  CLIENT_ID_RE,
  applyTrack,
  packExistsInIndex,
} = require("../lib/statsStore");

function safeJson(s) {
  try {
    return JSON.parse(s);
  } catch {
    return null;
  }
}

module.exports = async function handler(req, res) {
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type, Accept");
  res.setHeader("Cache-Control", "no-store");

  if (req.method === "OPTIONS") {
    return res.status(204).end();
  }
  if (req.method !== "POST") {
    return res.status(405).json({ error: "POST only" });
  }

  if (isRateLimited(req, 10)) {
    return res.status(429).json({ error: "rate limited — try later" });
  }

  const token = process.env.GITHUB_TOKEN;
  if (!token) {
    return res.status(503).json({ error: "server misconfigured (no token)" });
  }

  const body = typeof req.body === "string" ? safeJson(req.body) : req.body;
  if (!body || typeof body !== "object") {
    return res.status(400).json({ error: "invalid json" });
  }

  const op = String(body.op || "");
  if (op !== "download" && op !== "rate") {
    return res.status(400).json({ error: "invalid op" });
  }

  const id = String(body.id || "");
  if (!ID_RE.test(id)) {
    return res.status(400).json({ error: "invalid id" });
  }

  const clientId = String(body.clientId || "");
  if (!CLIENT_ID_RE.test(clientId)) {
    return res.status(400).json({ error: "invalid clientId" });
  }

  let stars = 0;
  if (op === "rate") {
    stars = Number(body.stars);
    if (!Number.isInteger(stars) || stars < 1 || stars > 5) {
      return res.status(400).json({ error: "stars must be 1-5" });
    }
  }

  try {
    const exists = await packExistsInIndex(token, id);
    if (!exists) {
      return res.status(400).json({ error: "unknown pack id" });
    }

    const result = await applyTrack(token, { op, id, clientId, stars });
    if (!result.ok) {
      const status = result.status || 400;
      return res.status(status).json({
        error: result.error || "track failed",
        downloads: result.downloads,
        ratingSum: result.ratingSum,
        ratingCount: result.ratingCount,
      });
    }

    return res.status(200).json({
      ok: true,
      duplicate: !!result.duplicate,
      updated: !!result.updated,
      id,
      downloads: result.downloads,
      ratingSum: result.ratingSum,
      ratingCount: result.ratingCount,
    });
  } catch (e) {
    const msg = e && e.message ? e.message : "track failed";
    return res.status(502).json({ error: msg });
  }
};
