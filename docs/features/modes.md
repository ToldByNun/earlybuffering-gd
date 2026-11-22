# Assist modes

Choose Live, Hybrid, or AI under Main → Assist Mode. Ctrl+Shift+M cycles between them.

| Mode | Current behavior |
|---|---|
| Live | Plans each physical press/release from the current game state |
| Hybrid | Tabular learning: with Auto-Tune enabled, learns timings across attempts and tests familiar timings in the Live input window. See Learn for this level’s progress. |
| AI | Neural candidate scorer (1.8.0): ranks delays to verify; Shadow physics still decides what may arm. Per-level `.neural` weights; packaged `resources/ai/prior.ebnn`. When a level already has Hybrid tables, AI blends those mature delays into its verify order (bootstrap — AI does not require Hybrid). |

**Learn** is Hybrid-only progress (familiar patterns, advanced tabular knobs, config I/O, Reset This Level). **AI** is its own tab for Candidate Scorer status, Autoplay / Train Session, top-k / learning-rate knobs, freeze, prior load, and **Visualize AI**. Auto-Tune (on Learn) still gates online AI weight writes. The Bake tab and macro/bake mode selection have been removed.

**Visualize AI** (`show-ai`, also on Visual): draws exact GD collision hitboxes (`getObjectRect` AABB like RobTop/OpenHack; OBB only when rotated and fresh): blue solid, red hazard, green orb/pad, yellow player. Scorer grid stamps those same rects. Ranked delay dots + train pulse. Draw only — never arms.

**AP-AUTOPLAY / AP-AUTOPLAY-2:** AI tab → **Autoplay** probes with the Candidate Scorer + a cheap `verifyDelay`, and only presses Jump when the click looks useful (or Explore fires). Not CPS spam. **Time Warp** (default 8x) speeds attempts; enable **Auto-Tune** so deaths write weights. Autoplay deaths auto-train only when the session buffer grew. Rewards prefer real Delay assists over Pass@d0 (avoids scorer collapse), shape by X-progress, and soft-punish recent edges on death. Manual **Train Session** remains for IL after human play; **Clear Session** drops the buffer only.

**Game knowledge vs Hybrid:** Features are a local hazard/solid/orb grid + continuous mode/speed state (transferable), plus a cheap shadow pair: **idle/coast vs action-now** (survive norms, endX delta, `actionHelps`, `idleSafer`). That tells the scorer whether a click is helpless or has a survival chance. Hybrid stores per-spot tables for one level. AI default saves per-level `.neural`; **Train Base Prior** writes a shared `saveDir/ai/prior.ebnn` across levels (build game knowledge first), then turn it off to fine-tune per level. Load order: user base prior → packaged `resources/ai/prior.ebnn`.

**Hybrid is tabular; AI is neural.** Both reuse the same propose → `verifyDelay` contract. Live is the base planner.

**Reset This Level** clears Hybrid tables (`.hybrid`) and AI weights (`.neural`) together. Auto-Tune trains AI; with Auto-Tune off, AI still infers if prior/level weights exist (Frozen).

**AI-first path:** confidence-ranked top-k → `verifyDelay` → arm a verified **delay>0** when any exists, else d=0 Pass, else Live fallthrough. Never arms from raw score. Failed verifies demote both confidence and rank. Live fallthrough Delays are weak teacher labels (not survived truth). Survived observation is the strongest evidence; identical-next buffer samples stay supervised-only (no fake TD). Autoplay schedules the verified delay instead of pressing immediately. Skips when Flight-owned, dual, or remUs is too low. Hybrid-first stays Hybrid-only (no double verify stack).

**AP-NL5 seed strategies (Autoplay/AI):** **Enhanced** (default) uses the configured confidence threshold + top-k, then Shadow verify — 0.6 is never implied. **NovaParity** is an explicit compare mode (duty bit + window subsample + hard 0.6). Live click-timing is separate and unchanged. Rollback is not ported.

**AP-NL5b ShadowVerified:** Autoplay executes a route only when every planned event was Shadow-simulated (horizon covers last event + settle, no unknown/budget). Partial or truncated routes stay `PartiallyChecked` and do not arm as safe; Explore presses stay labeled unverified.

**AP-NL6 Autoplay queue / metrics:** Autoplay can early-plan into a short route queue, optionally reuse the last ShadowVerified route, and soft-cap its own CPS — none of that changes Live windows. Success is scored with Autoplay DR4 metrics (route fidelity, verify completeness, progress); seed stats stay diagnostic only.

**AP-NL7 hook order:** Assist plans and Autoplay run in PrePhysicsAssist (before `PlayerObject::update`); injects use a single flag so they cannot re-enter Live planning. Platformer stays hard-off for Autoplay.

**AP-NL8 plan opt:** AI route planning caches windows, early-outs empty ranges, stride-subsamples NN evals, and shows seed/expand/verify µs on the overlay — budget aborts stay PartiallyChecked, never ShadowVerified.

**AI Live hitch wall (Nova-aligned):** Soft wall is the AP1 hitch ceiling (`sim-budget-us=0` → 8ms). Lookahead shrink compares remUs to the **frame** slice (floored ~3ms), not the hitch ceiling — otherwise Stage-12 collapses to d≤4 at 240Hz. AI-first spends a **half-press shell** (classic ≤2ms / half reserved — not Adaptive’s discrete half+ which starved AI to Shell=1); if AI finds nothing, classic Live still runs with its reserve restored. Live `prepare` skips the dual Shadow idle/action probe. Auto-Tune learning persists `.neural` every 16 attempt-boundary flushes.

**AP06 bootstrap:** In AI mode, existing `.hybrid` tables load read-only. Mature Hybrid proposals are merged ahead of neural ranks for verify order; while Auto-Tune trains, those proposals soft-distill into the scorer. Empty Hybrid tables are fine — AI runs on prior/weights alone.

Existing Live settings remain Live. Existing Hybrid settings stay Hybrid. Existing AI settings stay AI. Replay, BruteForce, Route and unknown imported mode values become Live. Legacy bake files remain on disk. Neither mode consumes them as timing templates.

Normally the player initiates every input; **AI Autoplay** is the opt-in exception for RL self-play. The planner can delay presses within the configured window. Press/release cleanup continues when changing modes.

**Flight:** The **Flight** tab holds **Straightfly Assist** / **Wave Pattern Assist**, angle snap, room-of-error, and live tracking status. **Visual** only draws (**Visualize Straightfly** / **Wave Pattern**). Dev session toggles still OR into assist for testing. Modes → Ship/Wave off is bypassed only when the assist enable is on — not by viz alone.

The planner always uses v1 and ShadowPlayer. Their former dropdowns have been removed.
