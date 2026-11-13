# Main & Window

## Enabled

Master switch. Off = fully vanilla gameplay.

## Safe Mode

See [Safe Mode](/features/safe-mode). Keep on unless you know why not.

## Assist Mode

Live / Hybrid / Replay / BruteForce — see [Modes](/features/modes).

## Window Unit

| Unit | Think of it as |
|---|---|
| **Ticks** | Physics steps (~240 per second on many setups) |
| **Frames** | Display frames (~60 fps). Converted ≈ **×4 ticks** at 240 TPS |

Planning always runs in ticks. If you switch unit, **re-set the numbers** so they match how you think.

## Window (Default) / Window (Wave)

How early a click may be delayed.

- Higher = more forgiveness, floatier
- Lower = snappier, closer to vanilla

Wave is sensitive — keep its window low until you have a reason.

## Smart / Adaptive (folded into Main)

Smart prediction knobs and Adaptive Plan toggles that used to sit under a separate Smart tab live here in **v1.6.x**. See [Adaptive Plan](/features/adaptive).

## Overlay / Pause UI

Desktop chrome (Accent Color, overlay / pause toggles, Lag Warning) lives under the **UI** tab. Pause EB still opens the ImGui shell on Windows (version label = package version, currently **1.6.1**).

| Setting | Effect |
|---|---|
| In-Level Overlay | Tiny status while playing |
| Pause EB Button | Top-right EB opens settings |
| Pause Status Lines | Extra pause text |
| Accent Color | Desktop menu theme + wave fill (UI tab) |
| Lag Warning | Once-per-level ImGui card when a Live click feels hitchy |

None of these change assist strength by themselves.
