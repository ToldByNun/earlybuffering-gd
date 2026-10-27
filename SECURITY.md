# Security

frirecracker is a local Geometry Dash client mod. It does not intentionally open network sockets or exfiltrate save data.

## Reporting

If you find a vulnerability in packaging, CI, or dependency handling, open a private report with the maintainer via GitHub Security Advisories (or a confidential issue contact if advisories are unavailable).

## Player safety

- Prefer **Safe Mode** so assisted runs do not write normal progress.
- Replay import and file pickers run locally; only open files you trust.
- Treat AI-assisted code as needing review; do not assume cryptographic or sandbox guarantees beyond what Geode provides.