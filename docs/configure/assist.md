# Assist settings

**v1.6.x** Assist tab covers **where** assist may run (practice / percent / off-ranges — formerly Scope) and **how** the planner thinks.

## Where it runs

See [Where it runs](/configure/scope). Platformer is always off.

The planner uses v1 with ShadowPlayer. Imported pick/engine choices from older configs are ignored.

## Orbs

When Assist Orbs is on, Live can look for orb / pad opportunities inside the window. Orb paths are picky — if orb assists feel wrong, turn orbs off and retest the same click.

## Budgets

| Idea | Meaning |
|---|---|
| Tick budget | Hard cap on how much sim work one press may do |
| Sim budget (µs) | Soft wall-clock limit |

If the planner hits budget often, you get more Pass / weaker search — not “stronger cheat”.

Phone: start lower / Adaptive off if the game stutters.

## Adaptive Plan

See [Adaptive Plan](/features/adaptive). Extra knobs (screen / verify / reserve) only matter when Adaptive is on.

## Learning / Auto Tune

Learn contains Auto Tune, reset learning and config import/export. The former self-bake workflow and Bake tab have been removed.

Trigger World preparation controls are under Assist. Hybrid currently uses Live planning while learned Hybrid is developed.
