# Assist modes

Setting: **Main → Assist Mode** (also cycle key, default Ctrl+Shift+M).

| Mode | What it does | When to use |
|---|---|---|
| **Live** | Plans every press/release from the current game state | Default. Start here |
| **Hybrid** | Uses a nearby **bake window** if it verifies safe, else falls back to Live | When you have a bake map for the level |
| **Replay** | Timing windows from an imported macro bake | Practice with a known click map |
| **BruteForce** | Windows from a brute bake (no replay file) | Heavy bake workflow |

## Important honesty

- Live / Hybrid never softlock a hopeless click — they **Pass** (vanilla).
- Replay / BruteForce are **timing maps**, not a live multi-click bot queue.
- Your finger still starts the click.

## Pick Mode (v1 / v2)

Under Assist settings:

- **v1** (default): if delay 0 is already safe → Pass now; else pick a robust later delay in the window.
- **v2**: reserved (currently same idea as v1).

## Assist Engine

- **ShadowPlayer** (default): full ghost player sim
- **Trajectory**: lighter path for some Live V2 paths

v1 always uses ShadowPlayer.

More detail: [How it works](/how-it-works/) and [Assist reference](/assist_reference).
