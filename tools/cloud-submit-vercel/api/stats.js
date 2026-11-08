/**
 * GET catalog stats (public counters only — no actor hash lists).
 */

const { loadStats, publicStatsView, emptyStats } = require("../lib/statsStore");

module.exports = async function handler(req, res) {
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type, Accept");
  res.setHeader("Cache-Control", "no-store");

  if (req.method === "OPTIONS") {
    return res.status(204).end();
  }
  if (req.method !== "GET") {
    return res.status(405).json({ error: "GET only" });
  }

  const token = process.env.GITHUB_TOKEN;
  if (!token) {
    // Still return empty shape so clients can open offline-ish
    return res.status(200).json(publicStatsView(emptyStats()));
  }

  try {
    const { stats } = await loadStats(token);
    return res.status(200).json(publicStatsView(stats));
  } catch (e) {
    const msg = e && e.message ? e.message : "stats load failed";
    return res.status(502).json({ error: msg });
  }
};
