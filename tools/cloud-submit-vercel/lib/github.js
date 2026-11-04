/**
 * Shared GitHub Contents helpers for the EarlyBuffer catalog API.
 */

const REPO = process.env.REPO || "ToldByNun/earlybuffer-configs";
const BRANCH = process.env.BRANCH || "main";

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
      `/repos/${REPO}/contents/${encodeURIComponent(path).replace(/%2F/g, "/")}?ref=${BRANCH}`
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
  return gh(
    token,
    "PUT",
    `/repos/${REPO}/contents/${encodeURIComponent(path).replace(/%2F/g, "/")}`,
    body
  );
}

function decodeContent(file) {
  if (!file || !file.content) return null;
  return Buffer.from(String(file.content).replace(/\n/g, ""), "base64").toString(
    "utf8"
  );
}

module.exports = {
  REPO,
  BRANCH,
  gh,
  getContent,
  putContent,
  decodeContent,
};
