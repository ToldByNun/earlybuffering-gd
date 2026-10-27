# frirecracker

Early-input **forgiveness assist** for Geometry Dash (Geode).

Click a bit early → the mod buffers the press and fires on the first tick the shadow simulator considers safe. Your clicks are never swallowed when the plan is hopeless; late clicks are not rewound.

## Modes

- **Live** — plan each press/release in real time
- **Hybrid** — use a baked window when near one, otherwise Live
- **Replay / BruteForce** — use precomputed click *windows* from a bake (timing map, not a live bot queue)

## Defaults worth knowing

- **Safe Mode** on — assisted / enabled runs do not count as normal progress
- **Platformer** — assist disabled
- Overlay and pause bake/assist UI can be turned off in settings

Design and requirements: maintainer. Implementation: AI-assisted (see repository README).