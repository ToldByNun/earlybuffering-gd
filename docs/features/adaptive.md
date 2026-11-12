# Adaptive Plan

Setting: **Main → Adaptive Prediction** (`adaptive-plan`).  
Per-mode override: **Modes → Adaptive Overwrite** (`*-adaptive-overwrite`).

## Plain English

Normal Live checks delays in a more equal way.  
**Adaptive** spends its time in steps:

1. **Screen** — quick look at many delays  
2. **Verify** — dig deeper on promising ones  
3. **Gate** — pick Pass or Delay honestly  

If Adaptive is unsure, it can fall back to classic Live planning with the **remaining** budget.

## When to turn it on

- Live already feels okay
- You want better decisions on hard timings
- You accept a bit more planner work per click

## When to leave it off

- Phone with tight budgets and lots of PassBudget
- You are still learning windows / Safe Mode
- You are bisecting a bug (“is Adaptive involved?”)

## Knobs (Main → Adaptive)

Names vary slightly in the UI, but the idea is:

- screen / verify tick slices
- reserve percent (keep some ticks for later)
- overall tick budget still caps the press

Main Adaptive is the **default / fallback**. Under **Modes**, turn **Adaptive Overwrite** on to use different Adaptive knobs for Wave/Ship/etc. without changing the Main defaults.

If Adaptive feels “always budget”, raise tick budget carefully or turn Adaptive off and retest.

Deep notes: [Assist reference](/assist_reference).
