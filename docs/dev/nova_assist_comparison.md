# Nova assist comparison (2026-11-23)

Target: EarlyBuffer 1.8.0 / GD 2.2081. Read-only analysis of the user's
`alquerez.nova.dll.i64`, attached through ida-mcp. Addresses below identify
functions in that database; decompiler variable names are not source names.
The search, physics runner and continuation routine were decompiled again for
this comparison. This is behavioral analysis, not a claim of identical physics.

## Observed differences

| Nova evidence | EarlyBuffer difference | Change / status |
| --- | --- | --- |
| `0x180236DA0` memoizes delay outcomes, refines the first safe boundary, then optionally searches the safe band's far boundary and verifies an interior choice. `0x18022FCB0` also searches for a sturdier alternative to an exact-tick save. | Live rescue stopped after its first safe boundary. | Live/AI now inspect at most four additional ticks after the first safe boundary and choose the midpoint of the widest contiguous tested safe band. Holes and unknowns break a band; ties prefer less latency. This bounds extra work rather than copying Nova's potentially longer band scan. |
| `0x18031F680` recognizes grounded action completion. At its unit tick scale the runner uses 15 airborne ticks and a tail strictly greater than four ticks after first contact, with special treatment for a gravity-changing Ball action. | The new rescue path used stable Ball completion only; Cube demanded the entire no-future-input horizon. | Enable Cube completion with 15 airborne and five contact ticks at 240 TPS; scale with simulation dt. Retain the existing stronger eight-contact-tick Ball check. Cube completion excludes orb activation, dashes, mode/gravity changes and unknown toggles. Both independent and shared-prefix runners use the same thresholds. |
| `0x180339FA0` explores possible subsequent inputs; `0x18033B3E0` distinguishes terminal, incomplete and completed outcomes. The caller can expand its deadline for unresolved continuation. | Ordinary rescue forbids hypothetical follow-up inputs and preserves one shared wall. | Not copied in this change: an assumed later press/release is conditional evidence, not a fully verified single-edge save. Existing opt-in continuation paths are separate. |
| `0x18022FCB0` has a dedicated orb timing scan, neighboring-timing checks and a moving-spike clearance preference. | Ordinary rescue does not reproduce this entire specialist pipeline. | Not claimed as parity. Do not replace missing clearance/continuation evidence with arbitrary end-X or late-death scores. |

## Invariants and limits

The original input is still preserved when proved safe. Delays never exceed the
configured Max Assist window. All selected ticks were simulated; no midpoint is
inferred safe from its endpoints. Band growth stops under budget pressure and
keeps an already proved save. Unknown, truncated and colliding runs remain
unproved even if an action-completion flag is present. No extra real press or
release is injected by these changes.

Offline checks exhaust all 10-tick safe-window masks in band mode, cover gaps,
isolated saves, unknown neighbors and budget pressure, and verify the actual
Cube landing tracker across airborne/contact durations. The recorded Wave gate
now uses the production interior-band policy. Build/install and gameplay must
still establish whether this improves actual rescue rates; offline tests cannot
establish Nova-equivalent gameplay or FPS.
