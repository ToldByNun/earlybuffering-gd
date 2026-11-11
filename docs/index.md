---
layout: home
hero:
  name: EarlyBuffer
  text: Click early. Still live.
  tagline: Timing assist for Geometry Dash 2.2081 — not an autoplayer. Install from GitHub (not the Geode Index). Docs for players and developers.
  actions:
    - theme: brand
      text: Quick start
      link: /start
    - theme: alt
      text: Install (GitHub)
      link: /install/
    - theme: alt
      text: Developer
      link: /dev/
features:
  - title: You still click
    details: EarlyBuffer only moves your jump a tiny bit later if that is safer. It never invents clicks for you.
  - title: Easy settings
    details: Start with Live mode, small windows, Safe Mode on. Change one thing at a time.
  - title: GitHub install
    details: Banned from the Geode Index — grab the .geode from Releases, or build from source.
---

## What you need to know in 20 seconds

1. You jump a little **too early**.
2. EarlyBuffer holds that press for a short moment.
3. A tiny “shadow” copy of the game checks “is this tick still safe?”
4. If yes → it fires your jump there. If nothing better → your normal click goes through.

That is the whole idea. Everything else (modes, windows, Adaptive, Cloud Configs) is just **how** it decides.

::: tip Age / vibe check
Player pages use short sentences on purpose. Devs: jump to [Developer](/dev/) or **Deep reference**.
:::

::: warning Geode Index
EarlyBuffer is **not** on the Geode Index. Use [GitHub Releases](/install/github-releases).
:::

## Next steps

- New here → [Quick start](/start)
- Need the mod file → [Install](/install/)
- Want to tweak → [Configure](/configure/)
- Something broke → [Problems](/troubleshoot/)
- Want internals → [Developer](/dev/)
