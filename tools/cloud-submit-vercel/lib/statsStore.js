/**
 * stats.json load / save / public strip / actor hash helpers.
 *
 * Rate rules: 1 clientId = 1 rating row per pack. Re-rate replaces stars
 * (ratingSum adjusts; ratingCount stays the same).
 */

const { createHash } = require("node:crypto");
const { getContent, putContent, decodeContent } = require("./github");

const STATS_PATH = "stats.json";
const MAX_HASHES = 5000;
const HASH_LEN = 16;
const ID_RE = /^[a-z0-9]([a-z0-9-]{0,62}[a-z0-9])?$/;
const CLIENT_ID_RE = /^[a-zA-Z0-9-]{32,64}$/;

function emptyStats() {
  return { schema: 1, updatedAt: new Date().toISOString(), packs: {} };
}

function ensurePack(stats, id) {
  if (!stats.packs || typeof stats.packs !== "object") stats.packs = {};
  if (!stats.packs[id] || typeof stats.packs[id] !== "object") {
    stats.packs[id] = {
      downloads: 0,
      ratingSum: 0,
      ratingCount: 0,
      dl: [],
      rt: [],
    };
  }
  const p = stats.packs[id];
  if (!Array.isArray(p.dl)) p.dl = [];
  if (!Array.isArray(p.rt)) p.rt = [];
  if (typeof p.downloads !== "number") p.downloads = 0;
  if (typeof p.ratingSum !== "number") p.ratingSum = 0;
  if (typeof p.ratingCount !== "number") p.ratingCount = 0;
  return p;
}

function actorHash(clientId, packId, op) {
  const salt = op === "rate" ? "rt" : "dl";
  return createHash("sha256")
    .update(`${clientId}|${packId}|${salt}`, "utf8")
    .digest("hex")
    .slice(0, HASH_LEN);
}

/** rt entries: legacy string hash, or { h, s }. */
function findRateEntry(pack, hash) {
  for (let i = 0; i < pack.rt.length; i++) {
    const e = pack.rt[i];
    if (typeof e === "string" && e === hash) {
      return { index: i, hash, stars: 0, legacy: true };
    }
    if (e && typeof e === "object" && e.h === hash) {
      const s = Number(e.s);
      return {
        index: i,
        hash,
        stars: Number.isInteger(s) && s >= 1 && s <= 5 ? s : 0,
        legacy: false,
      };
    }
  }
  return null;
}

/**
 * First vote or replace. Never bumps ratingCount on replace.
 * @returns {{ kind: "new"|"updated"|"same", error?: string, status?: number }}
 */
function applyRateToPack(pack, hash, stars) {
  const existing = findRateEntry(pack, hash);
  if (!existing) {
    if (pack.rt.length >= MAX_HASHES) {
      return { kind: "error", status: 503, error: "rating set full" };
    }
    pack.rt.push({ h: hash, s: stars });
    pack.ratingSum += stars;
    pack.ratingCount += 1;
    return { kind: "new" };
  }

  let oldStars = existing.stars;
  if (existing.legacy || oldStars < 1 || oldStars > 5) {
    // Legacy hash-only row: approximate previous vote with current average.
    oldStars =
      pack.ratingCount > 0
        ? Math.round(pack.ratingSum / pack.ratingCount)
        : stars;
    if (oldStars < 1) oldStars = 1;
    if (oldStars > 5) oldStars = 5;
  }

  if (oldStars === stars && !existing.legacy) {
    return { kind: "same" };
  }

  pack.ratingSum += stars - oldStars;
  if (pack.ratingSum < 0) pack.ratingSum = 0;
  pack.rt[existing.index] = { h: hash, s: stars };
  // ratingCount unchanged
  return { kind: "updated" };
}

function publicStatsView(stats) {
  const out = {
    schema: 1,
    updatedAt: stats.updatedAt || new Date().toISOString(),
    packs: {},
  };
  const packs = stats.packs && typeof stats.packs === "object" ? stats.packs : {};
  for (const [id, raw] of Object.entries(packs)) {
    if (!ID_RE.test(id) || !raw || typeof raw !== "object") continue;
    out.packs[id] = {
      downloads: Number(raw.downloads) || 0,
      ratingSum: Number(raw.ratingSum) || 0,
      ratingCount: Number(raw.ratingCount) || 0,
    };
  }
  return out;
}

async function loadStats(token) {
  const file = await getContent(token, STATS_PATH);
  if (!file) {
    return { stats: emptyStats(), sha: null };
  }
  const raw = decodeContent(file);
  let stats = emptyStats();
  if (raw) {
    try {
      const parsed = JSON.parse(raw);
      if (parsed && typeof parsed === "object") {
        stats = parsed;
        if (stats.schema !== 1) stats.schema = 1;
        if (!stats.packs || typeof stats.packs !== "object") stats.packs = {};
      }
    } catch {
      stats = emptyStats();
    }
  }
  return { stats, sha: file.sha || null };
}

async function saveStats(token, stats, sha, message) {
  stats.schema = 1;
  stats.updatedAt = new Date().toISOString();
  const text = `${JSON.stringify(stats, null, 2)}\n`;
  try {
    await putContent(token, STATS_PATH, message, text, sha || undefined);
    return { ok: true };
  } catch (e) {
    const msg = String(e.message || e);
    if (msg.includes("409") || /sha/i.test(msg) || /conflict/i.test(msg)) {
      return { ok: false, conflict: true, error: msg };
    }
    throw e;
  }
}

function countersOf(pack) {
  return {
    downloads: pack.downloads,
    ratingSum: pack.ratingSum,
    ratingCount: pack.ratingCount,
  };
}

/**
 * Apply track once; returns result object for HTTP layer.
 */
async function applyTrack(token, opts) {
  const { op, id, clientId, stars } = opts;

  let { stats, sha } = await loadStats(token);
  const pack = ensurePack(stats, id);
  const hash = actorHash(clientId, id, op);
  let rateKind = null;

  if (op === "download") {
    if (pack.dl.includes(hash)) {
      return { ok: true, duplicate: true, updated: false, ...countersOf(pack) };
    }
    if (pack.dl.length >= MAX_HASHES) {
      return { ok: false, status: 503, error: "download set full" };
    }
    pack.dl.push(hash);
    pack.downloads += 1;
  } else if (op === "rate") {
    const r = applyRateToPack(pack, hash, stars);
    if (r.kind === "error") {
      return { ok: false, status: r.status || 400, error: r.error, ...countersOf(pack) };
    }
    if (r.kind === "same") {
      return { ok: true, duplicate: true, updated: false, ...countersOf(pack) };
    }
    rateKind = r.kind;
  } else {
    return { ok: false, status: 400, error: "invalid op" };
  }

  let saved = await saveStats(token, stats, sha, `cloud-stats: ${op} ${id}`);
  if (!saved.ok && saved.conflict) {
    ({ stats, sha } = await loadStats(token));
    const pack2 = ensurePack(stats, id);
    const hash2 = actorHash(clientId, id, op);
    if (op === "download") {
      if (pack2.dl.includes(hash2)) {
        return { ok: true, duplicate: true, updated: false, ...countersOf(pack2) };
      }
      if (pack2.dl.length >= MAX_HASHES) {
        return { ok: false, status: 503, error: "download set full" };
      }
      pack2.dl.push(hash2);
      pack2.downloads += 1;
    } else {
      const r2 = applyRateToPack(pack2, hash2, stars);
      if (r2.kind === "error") {
        return {
          ok: false,
          status: r2.status || 400,
          error: r2.error,
          ...countersOf(pack2),
        };
      }
      if (r2.kind === "same") {
        return { ok: true, duplicate: true, updated: false, ...countersOf(pack2) };
      }
      rateKind = r2.kind;
    }
    saved = await saveStats(token, stats, sha, `cloud-stats: ${op} ${id} retry`);
    if (!saved.ok) {
      return { ok: false, status: 509, error: "stats write conflict" };
    }
    return {
      ok: true,
      duplicate: false,
      updated: op === "rate" && rateKind === "updated",
      ...countersOf(ensurePack(stats, id)),
    };
  }

  if (!saved.ok) {
    return { ok: false, status: 502, error: saved.error || "stats write failed" };
  }

  return {
    ok: true,
    duplicate: false,
    updated: op === "rate" && rateKind === "updated",
    ...countersOf(pack),
  };
}

async function packExistsInIndex(token, id) {
  const file = await getContent(token, "index.json");
  if (!file) return false;
  const raw = decodeContent(file);
  if (!raw) return false;
  try {
    const index = JSON.parse(raw);
    if (!index || !Array.isArray(index.packs)) return false;
    return index.packs.some((p) => p && p.id === id);
  } catch {
    return false;
  }
}

module.exports = {
  ID_RE,
  CLIENT_ID_RE,
  MAX_HASHES,
  emptyStats,
  publicStatsView,
  loadStats,
  applyTrack,
  applyRateToPack,
  findRateEntry,
  packExistsInIndex,
};
