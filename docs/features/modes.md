# Assist modes

Choose Live, Hybrid, or AI under Main → Assist Mode. Ctrl+Shift+M cycles between them.

| Mode | Current behavior |
|---|---|
| Live | Plans each physical press/release from the current game state |
| Hybrid | Tabular learning: with Auto-Tune enabled, learns timings across attempts and tests familiar timings in the Live input window. See Learn for this level’s progress. |
| AI | Neural candidate scorer (1.8.0): ranks delays to verify; Shadow physics still decides what may arm. Per-level `.neural` weights; packaged `resources/ai/prior.ebnn`. When a level already has Hybrid tables, AI blends those mature delays into its verify order (bootstrap — AI does not require Hybrid). |

**Learn** is Hybrid-only progress (familiar patterns, advanced tabular knobs, config I/O, Reset This Level). **AI** is its own tab for Candidate Scorer status, top-k / learning-rate knobs, freeze, and prior load. Auto-Tune (on Learn) still gates AI training writes. The Bake tab and macro/bake mode selection have been removed.

**Hybrid is tabular; AI is neural.** Both reuse the same propose → `verifyDelay` contract. Live is the base planner.

**Reset This Level** clears Hybrid tables (`.hybrid`) and AI weights (`.neural`) together. Auto-Tune trains AI; with Auto-Tune off, AI still infers if prior/level weights exist (Frozen).

**AI-first path:** score → top-k → `verifyDelay` → arm, else Live fallthrough. Never arms from raw score. Skips when Flight-owned, dual, or remUs is too low. Hybrid-first stays Hybrid-only (no double verify stack). Quality = packaged prior + Shadow verify + on-play fine-tune — not offline AGI.

**AP06 bootstrap:** In AI mode, existing `.hybrid` tables load read-only. Mature Hybrid proposals are merged ahead of neural ranks for verify order; while Auto-Tune trains, those proposals soft-distill into the scorer. Empty Hybrid tables are fine — AI runs on prior/weights alone.

Existing Live settings remain Live. Existing Hybrid settings stay Hybrid. Existing AI settings stay AI. Replay, BruteForce, Route and unknown imported mode values become Live. Legacy bake files remain on disk. Neither mode consumes them as timing templates.

The player initiates every input. The planner can delay it within the configured window. Press/release cleanup continues when changing modes.

**Flight:** The **Flight** tab holds **Straightfly Assist** / **Wave Pattern Assist**, angle snap, room-of-error, and live tracking status. **Visual** only draws (**Visualize Straightfly** / **Wave Pattern**). Dev session toggles still OR into assist for testing. Modes → Ship/Wave off is bypassed only when the assist enable is on — not by viz alone.

The planner always uses v1 and ShadowPlayer. Their former dropdowns have been removed.
