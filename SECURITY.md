# Security

EarlyBuffer v1.4.3 includes optional Cloud Configs. Opening the browser fetches the public catalog and statistics over HTTPS. Installing downloads a settings pack. Sharing submits exported settings and the pack name, author, description and tags you enter. Download tracking and ratings use a persistent anonymous client ID. The service also receives connection metadata such as your IP address.

Cloud imports skip keybinds and developer settings. Service URLs are built into the mod. GitHub publishing credentials belong on the submission server. See [Cloud Configs](docs/cloud_configs.md) for endpoints, caching and server setup.

## Reporting

If you find a vulnerability in packaging, CI, or dependency handling, open a private report with the maintainer via GitHub Security Advisories (or a confidential issue contact if advisories are unavailable).

## Player safety

- Prefer **Safe Mode** so assisted runs do not write normal progress.
- Replay import and file pickers run locally; only open files you trust.
- Treat AI-assisted code as needing review; do not assume cryptographic or sandbox guarantees beyond what Geode provides.