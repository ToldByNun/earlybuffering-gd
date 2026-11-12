# Contributing

Also see repo `CONTRIBUTING.md` and `AGENTS.md`.

## Expectations

- Human-directed product; AI drafting is fine if reviewed
- Small, testable PRs
- **v1.6.x** targets GD **2.2081** only — do not casually mix newer GD bindings

## PR checklist

- [ ] Builds (`geode build` or CI green)
- [ ] No drive-by renames
- [ ] Settings + docs updated together when user-facing
- [ ] Honest wording for Replay / Brute / Safe Mode / Trigger World
- [ ] Style script clean
- [ ] Short PR description: **why**, not only what

## Distribution

Not shipped via Geode Index (banned). Releases go through GitHub / direct packages. Multi-platform zips must actually contain the platform binaries — run `tools/geode_package_check.py`.

## Cloud / secrets

Submission credentials belong in **server** env vars, never in the client repo. See [Cloud configs](/cloud_configs).
