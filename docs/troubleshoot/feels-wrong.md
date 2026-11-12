# Feels wrong / too strong / does nothing

## Too floaty / “cheaty”

1. Lower **Window (Default)** and Wave window  
2. Confirm **Window Unit** is what you think ([Ticks](/how-it-works/ticks))  
3. Turn **Adaptive** off for a bisect  
4. Turn **Orbs** off and retest  
5. Check you are not in Frames with a large number

## Does nothing

1. **Enabled** on?  
2. [Where it runs](/configure/scope) (Assist tab): practice-only? percent min/max? off-range covering this part?  
3. Platformer? Always off.  
4. Assist Mode Replay/Brute with **no bake map** for this level → nothing useful to apply  
5. Overlay on → are you seeing PassBudget / PassHopeless every time?  
6. **Window = 1 tick** (or “1 frame” while Unit = Ticks): cube often cannot clear the spike in one physics step — raise to ~4 ticks, or set Unit = **Frames** if you meant display frames (1 frame ≈ 4 ticks at 240 TPS)

## Helps early, dies later

The plan only looks a limited distance ahead. Movers the ghost cannot see can fake a safe Delay. Try Trigger World Bake, or update to a build with stronger unknown / honesty fixes.

## Eval says bestSurv=X but Live picked something else

Live may stop early (especially some input-window paths) or prefer earliest-safe on continuous modes. Discrete input-window scans the delay band; ball landing no longer inflates later delays via `surviveBin`. Report the session / report line if it still mismatches after update.
