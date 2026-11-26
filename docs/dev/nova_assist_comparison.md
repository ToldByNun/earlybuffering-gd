# Nova assist comparison (2026-11-23)

## Wave recheck (2026-11-25, installed Nova binary)

The currently open `C:/Users/mikaj/Pictures/alquerez.nova.dll` has different
function addresses from the earlier database. The older addresses below must
not be used against this binary. Read-only evidence was saved under
`build/nova_wave_20261125/` (generated, outside authored sources).

* Input routine `0x1802847F0` calls GD push/release for simulated edges, through
  wrappers `0x18060B350` / `0x18060B920`. EarlyBuffer's ghost release previously
  only changed the held map. It now executes native release with the existing
  ghost-only streak VFX guards, preserving GD bookkeeping and return value.
* Runner `0x18033A6D0` clears contact dictionaries via `0x18021BB80`, then applies
  the edge before movement and collision. That cleanup only removes entries
  from four dictionaries. EarlyBuffer now preserves the Wave's last contact
  IDs and omits its extra collision-only prepass for delayed Wave candidates.
  Independent and shared-prefix Wave candidates both start with movement.
* The runner uses `a8 + a9` as its loop bound, with the input edge at `a8`:
  delay plus post-edge lookahead. Strict Wave rescue now uses that timing in
  both runners and classification. Other modes keep their common horizon.
  The selected configured/adaptive cap still determines lookahead; this does
  not assert identical Nova settings or CPU adaptation.
* Retry caller containing `0x180220F4C` excludes deaths with an identified
  object before attempting the early Wave slope retry. EarlyBuffer now keeps
  real hazard deaths, restores the captured world before retrying and clears
  the first attempt's result. Its existing slope-state fallback remains.

The new production-body test checks native release delegation, both return
values, suppressed ghost fade-out effects, unchanged live effects and retained
Wave contact IDs. Recorded Wave outcomes additionally exercise the post-edge
horizon. Build and offline checks do not establish gameplay parity: Nova's
first-step crush veto, continuation search and full timing-band algorithm still
differ. No live FPS or 1:1 gameplay result is claimed.

Target: EarlyBuffer 1.8.0 / GD 2.2081. Read-only analysis of the user's
`alquerez.nova.dll.i64`, attached through ida-mcp. Addresses below identify
functions in that database; decompiler variable names are not source names.
The search, physics runner and continuation routine were decompiled again for
this comparison. This is behavioral analysis, not a claim of identical physics.

## Observed differences

| Nova evidence | EarlyBuffer difference | Change / status |
| --- | --- | --- |
| `0x180236DA0` memoizes delay outcomes, refines the first safe boundary, then optionally searches the safe band's far boundary and verifies an interior choice. `0x18022FCB0` also searches for a sturdier alternative to an exact-tick save. | Live rescue stopped after its first safe boundary. | Live/AI now inspect at most four additional ticks after the first safe boundary and choose the midpoint of the widest contiguous tested safe band. Holes and unknowns break a band; ties prefer less latency. This bounds extra work rather than copying Nova's potentially longer band scan. |
| `0x18031F680` recognizes grounded action completion. At its unit tick scale the runner uses 15 airborne ticks and a tail strictly greater than four ticks after first contact, with special treatment for a gravity-changing Ball action. | The new rescue path used stable Ball completion only; Cube demanded the entire no-future-input horizon. | Enable Cube completion with 15 airborne and five contact ticks at 240 TPS; scale with simulation dt. Retain the existing stronger eight-contact-tick Ball check. Cube completion excludes dashes, mode changes and unknown toggles. A modeled, actually activated orb can complete an airborne/grounded action even when it flips gravity; ordinary jumps retain the same-gravity requirement. Both independent and shared-prefix runners use the same thresholds. |
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

## Research 20261124_000327

The supplied session contains 48 Cube plans: five Delay decisions, 20 d0-safe
passes and 23 hopeless passes, with no plan-budget failures. It does not contain
Wave/Dual planning evidence for the accompanying screenshot. Several Cube
sequences include real orb-related gravity changes while every candidate is
rejected at a later collision. The old Cube completion gate explicitly excluded
all ring activation, so this class could never use the physical completion
boundary. The new gate permits actually modeled ring activation, but still
requires a full airborne/stable-contact sequence and checks death/unknown first.
This is a code-path correction; those captures cannot prove the new game's
counterfactual result without another live run. Candidate details now include
jump/activation/ring-count evidence for subsequent diagnosis.
