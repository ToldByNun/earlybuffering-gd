"""Earliness / lateness synthesizer for gold clears (APR1).

Families (locked):
  on-time                 — unchanged copy
  edge-only-early-k       — move press only to F-k (hold lengthens)
  pulse-shift-early-k     — move press+release by -k (hold preserved)
  edge-only-late-k        — move press only to F+k
  pulse-shift-late-k      — move press+release by +k
  drop-click              — remove one press + matching release

P1/P2 are clamped independently so edges never cross neighbors.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Literal

from .gdr2 import Gdr2Input, Gdr2Replay, clone_replay

Family = Literal[
    "on-time",
    "edge-only-early",
    "pulse-shift-early",
    "edge-only-late",
    "pulse-shift-late",
    "drop-click",
]


@dataclass(frozen=True)
class ClickPair:
    player2: bool
    button: int
    press_index: int
    release_index: int
    press_frame: int
    release_frame: int

    @property
    def hold_length(self) -> int:
        return self.release_frame - self.press_frame


@dataclass
class ShiftResult:
    family: str
    k: int
    click_index: int
    replay: Gdr2Replay
    sidecar: dict
    clamped: bool


def pair_clicks(inputs: list[Gdr2Input]) -> list[ClickPair]:
    pairs: list[ClickPair] = []
    for player2 in (False, True):
        open_press: dict[int, int] = {}
        for index, event in enumerate(inputs):
            if event.player2 != player2:
                continue
            if event.down:
                open_press[event.button] = index
                continue
            press_index = open_press.pop(event.button, None)
            if press_index is None:
                continue
            press = inputs[press_index]
            if event.frame < press.frame:
                continue
            pairs.append(
                ClickPair(
                    player2=player2,
                    button=event.button,
                    press_index=press_index,
                    release_index=index,
                    press_frame=press.frame,
                    release_frame=event.frame,
                )
            )
    pairs.sort(key=lambda item: (item.press_frame, item.player2, item.press_index))
    return pairs


def default_click_index(pairs: list[ClickPair]) -> int:
    if not pairs:
        raise ValueError("no paired Jump clicks in replay")
    return len(pairs) // 2


def family_name(family: Family, k: int = 0) -> str:
    if family == "on-time":
        return "on-time"
    if family == "drop-click":
        return "drop-click"
    return f"{family}-{k}"


def _same_player_edges(inputs: list[Gdr2Input], player2: bool) -> list[tuple[int, int]]:
    return [(i, ev.frame) for i, ev in enumerate(inputs) if ev.player2 == player2]


def _neighbor_bounds(
    inputs: list[Gdr2Input], pair: ClickPair
) -> tuple[int, int, int, int]:
    """Return (press_lo, press_hi, release_lo, release_hi) inclusive frame bounds."""
    edges = _same_player_edges(inputs, pair.player2)
    press_lo = 0
    press_hi = pair.release_frame - 1
    release_lo = pair.press_frame + 1
    release_hi = 10**9
    for index, frame in edges:
        if index == pair.press_index or index == pair.release_index:
            continue
        if index < pair.press_index:
            press_lo = max(press_lo, frame + 1)
        elif index > pair.press_index and frame < pair.release_frame:
            press_hi = min(press_hi, frame - 1)
        if index < pair.release_index and frame > pair.press_frame:
            release_lo = max(release_lo, frame + 1)
        if index > pair.release_index:
            release_hi = min(release_hi, frame - 1)
    return press_lo, press_hi, release_lo, release_hi


def _clamp(value: int, lo: int, hi: int) -> tuple[int, bool]:
    clamped = False
    if value < lo:
        value = lo
        clamped = True
    if value > hi:
        value = hi
        clamped = True
    return value, clamped


def _plan_shift_frames(
    inputs: list[Gdr2Input], pair: ClickPair, family: Family, k: int
) -> tuple[int, int, bool]:
    """Return (new_press, new_release, clamped) for one pair against current inputs."""
    if k < 0:
        raise ValueError("k must be >= 0")
    if k == 0:
        return pair.press_frame, pair.release_frame, False

    press_lo, press_hi, release_lo, release_hi = _neighbor_bounds(inputs, pair)
    clamped = False
    if family.endswith("early"):
        new_press, c1 = _clamp(pair.press_frame - k, press_lo, press_hi)
        clamped |= c1
        if family.startswith("pulse-shift"):
            delta = pair.press_frame - new_press
            new_release, c2 = _clamp(
                pair.release_frame - delta, max(release_lo, new_press + 1), release_hi
            )
            clamped |= c2
        else:
            new_release = pair.release_frame
            if new_press >= new_release:
                new_press = new_release - 1
                clamped = True
    elif family.endswith("late"):
        new_press, c1 = _clamp(
            pair.press_frame + k, press_lo, min(press_hi, pair.release_frame - 1)
        )
        clamped |= c1
        if family.startswith("pulse-shift"):
            delta = new_press - pair.press_frame
            new_release, c2 = _clamp(
                pair.release_frame + delta, max(release_lo, new_press + 1), release_hi
            )
            clamped |= c2
        else:
            new_release = pair.release_frame
            if new_press >= new_release:
                new_press = new_release - 1
                clamped = True
    else:
        raise ValueError(f"unknown family {family}")
    return new_press, new_release, clamped


def apply_shift(
    replay: Gdr2Replay,
    family: Family,
    k: int = 0,
    click_index: int | None = None,
) -> ShiftResult:
    if click_index is None and family != "on-time":
        pairs = pair_clicks(replay.inputs)
        click_index = default_click_index(pairs)
    return apply_multi_shift(
        replay,
        family,
        k=k,
        click_indices=[] if family == "on-time" else [int(click_index)],
    )


def apply_multi_shift(
    replay: Gdr2Replay,
    family: Family,
    k: int = 0,
    click_indices: list[int] | None = None,
) -> ShiftResult:
    """Shift one or more paired clicks (consecutive sequences for dense multi-press tests)."""
    inputs = [
        Gdr2Input(frame=e.frame, button=e.button, player2=e.player2, down=e.down)
        for e in replay.inputs
    ]
    pairs = pair_clicks(inputs)
    name = family_name(family, k)

    if family == "on-time":
        out = clone_replay(replay, inputs)
        sidecar = {
            "schema": 1,
            "family": name,
            "k": 0,
            "clickIndex": None,
            "clickIndices": [],
            "sequenceLength": 0,
            "shiftedIndices": [],
            "originalHoldLength": None,
            "newHoldLength": None,
            "clamped": False,
            "capture": {
                "mode": "none",
                "priorEventCount": 0,
                "captureFrame": None,
                "notes": "on-time has no shifted press",
            },
        }
        return ShiftResult(name, 0, -1, out, sidecar, False)

    if not pairs:
        raise ValueError("no paired Jump clicks to shift")
    indices = list(click_indices or [])
    if not indices:
        indices = [default_click_index(pairs)]
    for click_index in indices:
        if click_index < 0 or click_index >= len(pairs):
            raise ValueError(f"click_index {click_index} out of range 0..{len(pairs)-1}")
    # Stable ascending by gold press so earlier edits settle before later neighbor clamps.
    ordered = sorted(indices, key=lambda idx: (pairs[idx].press_frame, idx))
    primary = ordered[0]
    sequence_length = len(ordered)

    if family == "drop-click":
        drop: set[int] = set()
        removed_clicks = []
        for click_index in ordered:
            pair = pairs[click_index]
            drop.add(pair.press_index)
            drop.add(pair.release_index)
            removed_clicks.append(click_index)
        kept = [ev for i, ev in enumerate(inputs) if i not in drop]
        out = clone_replay(replay, kept)
        first = pairs[ordered[0]]
        sidecar = {
            "schema": 1,
            "family": name,
            "k": 0,
            "clickIndex": primary,
            "clickIndices": ordered,
            "sequenceLength": sequence_length,
            "player2": first.player2,
            "button": first.button,
            "shiftedIndices": [],
            "removedIndices": sorted(drop),
            "removedClicks": removed_clicks,
            "originalHoldLength": first.hold_length,
            "newHoldLength": None,
            "clamped": False,
            "goldPressFrame": first.press_frame,
            "goldReleaseFrame": first.release_frame,
            "capture": {
                "mode": "pre_would_be_press",
                "priorEventCount": sum(1 for ev in kept if ev.frame < first.press_frame),
                "captureFrame": first.press_frame,
                "notes": "drop-click sequence: capture before first missing press",
            },
        }
        return ShiftResult(name, 0, primary, out, sidecar, False)

    if k < 0:
        raise ValueError("k must be >= 0")

    edits = []
    clamped_any = False
    # Mutate in-place by original indices (no re-sort until the end).
    for click_index in ordered:
        pair = pairs[click_index]
        # Refresh pair frames from current inputs (prior edits may have moved neighbors only).
        live = ClickPair(
            player2=pair.player2,
            button=pair.button,
            press_index=pair.press_index,
            release_index=pair.release_index,
            press_frame=inputs[pair.press_index].frame,
            release_frame=inputs[pair.release_index].frame,
        )
        new_press, new_release, clamped = _plan_shift_frames(inputs, live, family, k)
        clamped_any |= clamped
        inputs[pair.press_index].frame = new_press
        if family.startswith("pulse-shift"):
            inputs[pair.release_index].frame = new_release
        edits.append(
            {
                "clickIndex": click_index,
                "player2": pair.player2,
                "button": pair.button,
                "goldPressFrame": pair.press_frame,
                "goldReleaseFrame": pair.release_frame,
                "newPressFrame": new_press,
                "newReleaseFrame": (
                    new_release if family.startswith("pulse-shift") else pair.release_frame
                ),
                "originalHoldLength": pair.hold_length,
                "newHoldLength": (
                    (new_release if family.startswith("pulse-shift") else pair.release_frame)
                    - new_press
                ),
                "clamped": clamped,
            }
        )

    out = clone_replay(replay, inputs)
    out.inputs.sort(key=lambda item: (item.frame, item.player2, item.button, 0 if item.down else 1))

    first_edit = edits[0]
    new_press = int(first_edit["newPressFrame"])
    press_indices = [
        i
        for i, ev in enumerate(out.inputs)
        if ev.frame == new_press
        and ev.player2 == first_edit["player2"]
        and ev.button == first_edit["button"]
        and ev.down
    ]
    prior = press_indices[0] if press_indices else 0
    sidecar = {
        "schema": 1,
        "family": name,
        "baseFamily": family,
        "k": k,
        "clickIndex": primary,
        "clickIndices": ordered,
        "sequenceLength": sequence_length,
        "player2": first_edit["player2"],
        "button": first_edit["button"],
        "shiftedIndices": press_indices[:1],
        "edits": edits,
        "originalHoldLength": first_edit["originalHoldLength"],
        "newHoldLength": first_edit["newHoldLength"],
        "clamped": clamped_any,
        "goldPressFrame": first_edit["goldPressFrame"],
        "goldReleaseFrame": first_edit["goldReleaseFrame"],
        "newPressFrame": first_edit["newPressFrame"],
        "newReleaseFrame": first_edit["newReleaseFrame"],
        "capture": {
            "mode": "pre_shifted_press",
            "priorEventCount": prior,
            "captureFrame": new_press,
            "notes": (
                "Replay prior events, capture before the first shifted press in the sequence. "
                "Multi-click sequences exercise consecutive assists / smart-depth paths."
            ),
        },
    }
    return ShiftResult(name, k, primary, out, sidecar, clamped_any)


def events_equal(a: list[Gdr2Input], b: list[Gdr2Input]) -> bool:
    if len(a) != len(b):
        return False
    for left, right in zip(a, b):
        if (
            left.frame != right.frame
            or left.button != right.button
            or left.player2 != right.player2
            or left.down != right.down
        ):
            return False
    return True
