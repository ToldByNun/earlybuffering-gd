/**
 * Best-effort in-memory IP rate limit (per serverless isolate).
 * Soft abuse brake only — identity for 1:1 rules is clientId.
 */

const buckets = new Map();

function clientIp(req) {
  const xf = req.headers["x-forwarded-for"];
  if (typeof xf === "string" && xf.length) {
    return xf.split(",")[0].trim() || "unknown";
  }
  return req.headers["x-real-ip"] || "unknown";
}

/**
 * @returns {boolean} true if over limit
 */
function isRateLimited(req, maxPerMinute = 10) {
  const ip = clientIp(req);
  const minute = Math.floor(Date.now() / 60000);
  const key = `${ip}:${minute}`;
  const cur = buckets.get(key) || 0;
  if (cur >= maxPerMinute) return true;
  buckets.set(key, cur + 1);
  // Drop old keys opportunistically
  if (buckets.size > 5000) {
    for (const k of buckets.keys()) {
      if (!k.endsWith(`:${minute}`) && !k.endsWith(`:${minute - 1}`)) {
        buckets.delete(k);
      }
    }
  }
  return false;
}

module.exports = { clientIp, isRateLimited };
