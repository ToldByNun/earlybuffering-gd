# EarlyBuffer

Early-input **forgiveness assist** for Geometry Dash **2.2081** (Geode).

Click a bit early → the mod buffers the press and fires on the first tick the shadow simulator considers safe. Your clicks are never swallowed when the plan is hopeless; late clicks are not rewound.

**v1.2.0 is the last release for GD 2.2081.** Newer game versions need a separate port.

## Modes

- **Live** — plan each press/release in real time
- **Hybrid** — use a baked window when near one, otherwise Live
- **Replay / BruteForce** — use precomputed click *windows* from a bake (timing map, not a live bot queue)

## Defaults worth knowing

- **Safe Mode** on — assisted / enabled runs do not count as normal progress
- **Platformer** — assist disabled
- Overlay and pause **EB** hub can be turned off in settings
- **Save Feedback** — Sound / Color / Counter toggles (sound + color on by default)
- **Trigger World Bake** on — static movers cached for the shadow sim

Actively maintained for 2.2081: reviewed, playtested, and still being optimized. See the repository README for authorship notes and the full feature list.
