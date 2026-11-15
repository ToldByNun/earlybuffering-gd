# Assist modes

Choose Live or Hybrid under Main → Assist Mode. Ctrl+Shift+M cycles between them.

| Mode | Current behavior |
|---|---|
| Live | Plans each physical press/release from the current game state |
| Hybrid | Currently uses Live planning. Learned candidate weighting is scheduled for AP09 |

Learn contains Auto Tune, learning reset and config import/export. The Bake tab and macro/bake mode selection have been removed.

Existing Live settings remain Live. Existing Hybrid settings become the new Hybrid selection. Replay, BruteForce, Route and unknown imported mode values become Live. Legacy bake files remain on disk. Neither mode consumes them as timing templates.

The player initiates every input. The planner can delay it within the configured window. Press/release cleanup continues when changing modes.

The planner always uses v1 and ShadowPlayer. Their former dropdowns have been removed.
