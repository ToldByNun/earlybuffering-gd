# Assist modes

Choose Live, Hybrid, or AI under Main → Assist Mode. Ctrl+Shift+M cycles between them.

| Mode | Current behavior |
|---|---|
| Live | Plans each physical press/release from the current game state |
| Hybrid | Tabular learning: with Auto-Tune enabled, learns timings across attempts and tests familiar timings in the Live input window. See Learn for this level’s progress. |
| AI | Neural candidate scorer (1.8.0): ranks delays to verify; Shadow physics still decides what may arm. Until the network lands, AI falls through to Live planning so UI/cycle stay safe. |

Learn contains Auto Tune, learning reset and config import/export. The Bake tab and macro/bake mode selection have been removed.

**Hybrid is tabular; AI is neural.** Both reuse the same propose → `verifyDelay` contract. Live is the base planner.

Existing Live settings remain Live. Existing Hybrid settings stay Hybrid. Existing AI settings stay AI. Replay, BruteForce, Route and unknown imported mode values become Live. Legacy bake files remain on disk. Neither mode consumes them as timing templates.

The player initiates every input. The planner can delay it within the configured window. Press/release cleanup continues when changing modes.

The planner always uses v1 and ShadowPlayer. Their former dropdowns have been removed.
