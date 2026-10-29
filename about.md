# PLEASE READ THIS

EarlyBuffer was written with a lot of AI-assisted drafting.

**That does not mean it was dumped unreviewed.**

Every line that ships was checked by hand. Behavior was playtested in-game, edge cases were chased down, and the planner / shadow path was exercised until the assist felt right — not “looks fine in a chat window.”

If you are reviewing this for the Geode Index (or as a player who cares how mods are made): treat it as human-directed engineering with AI as a tool, not as an unattended codegen paste.

---

# EarlyBuffer

**Click early. Still live.**

EarlyBuffer is a timing assist for Geometry Dash **2.2081**.
You jump a little soon — the mod holds that press and lands it on the first physics tick a **shadow simulation** still considers safe.

You still play the level. EarlyBuffer does not invent clicks, farm clears, or drive the icon for you. It only decides *when* a press you already made is allowed to land — inside the windows and modes you set.

> **v1.2.0** is the last EarlyBuffer package for **GD 2.2081**. Newer game builds need a separate port.

---

## What happens on a press

1. You hit Jump (a bit early).
2. A short shadow plan checks hazards, orbs, and — if you baked them — movers / toggles.
3. Safer tick later in your window → **buffer**, fire then.
4. Nothing better → **Pass**. Vanilla click goes through. Hopeless presses are not softlocked forever.
5. Releases work the same idea (ship / wave / short-hops), with one hard rule: **while you still hold Jump, EarlyBuffer never forces a release.** After you let go it may stretch the hold by at most your tick window.

---

## Modes

- **Live** — plans every press / release from the current state
- **Hybrid** — nearby bake window when it verifies; otherwise Live
- **Replay** — click windows from an imported replay bake
- **BruteForce** — windows from a no-replay brute bake

Bakes are **timing maps**, not a bot queue. Intent still comes from you.

---

## Defaults that matter

- **Safe Mode** on — assisted / enabled runs should not write normal progress for rated or online play
- **Platformer** hard off
- Presets (**Tight → Max**) fill tick windows · F6 cycles · Off kills assist
- Optional Trigger World Bake so movers aren’t invisible to the shadow
- Save Feedback: sound / flash / world `+N` and `8+` when a delay actually fired
- Lag Probe & Perf Trace if you’re hunting hitches

Every important setting has plain-language info text in the mod (what it does, what higher / lower means).

---

## Links

- **Source:** [ToldByNun/earlybuffering-gd](https://github.com/ToldByNun/earlybuffering-gd)
- **Bugs:** [GitHub Issues](https://github.com/ToldByNun/earlybuffering-gd/issues)
- **Chat:** [Discord](https://discord.gg/HAjrey6ykc)

Use Safe Mode. Practice. Don’t drop assisted runs on rated / online like they’re clean.
