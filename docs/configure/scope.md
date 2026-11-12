# Where assist is allowed (Assist tab)

In **v1.6.0** these gates live under **Assist** (desktop menu and classic filter). Platformer is **always** blocked. Everything else is optional.

## Practice only

On = assist only in Practice Mode.  
Off = also in normal mode (still respecting Safe Mode + other gates).

## Start-pos / test only

On = only in start-pos / test style play.

## Percent Min / Max

Assist may run only between these percents (0–100).

Examples:

- Raise Min to keep the start vanilla
- Lower Max to finish the last % without assist

## Assist Off Ranges

Percent holes **inside** the min–max window where assist is forced off (inclusive).

**Desktop menu:** add/remove list (soft cap 32). Stored as `assist-off-ranges` JSON `[[from,to],…]`.

**Classic Geode popup:** up to six From/To rows. Leave **-1** to ignore a slot. Those rows migrate into / mirror the first six JSON entries.

Example: Min 0, Max 100, Off Range 40–55 → assist skips that mid section.
