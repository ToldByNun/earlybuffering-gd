# Ticks vs frames

This confuses almost everyone once. Then it clicks.

## Tick

A **physics step**. Geometry Dash often runs many ticks per real second (often around **240**). Assist math uses ticks.

## Frame

What you see on screen (~**60** per second). One display frame is often about **4 ticks** at 240 TPS.

## So what?

| You set | Rough feel at 240 TPS |
|---|---|
| Window **4 ticks** | About **1** display frame of delay room |
| Window **4 frames** (unit = Frames) | About **16 ticks** of delay room |

If assist feels huge after you “only set 4”, check **Window Unit**. You may be in Frames without noticing.

::: tip Rule
Switch unit → then re-set the numbers. Do not assume the old number still means the same thing.
:::

::: warning Window = 1 on cube
**1 tick** is one physics step — often too small for cube miss-assist to move you past a spike (Delay has nowhere to go).  
**1 frame** (Unit = Frames) is about **4 ticks**. If assist “does nothing” on cube, check Unit first, then raise the window a little.
:::
