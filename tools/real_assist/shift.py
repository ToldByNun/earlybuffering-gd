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


def apply_shift(
    replay: Gdr2Replay,
    family: Family,
    k: int = 0,
    click_index: int | None = None,
) -> ShiftResult:
    inputs = [
        Gdr2Input(frame=e.frame, button=e.button, player2=e.player2, down=e.down)
        for e in replay.inputs
    ]
    pairs = pair_clicks(inputs)
    if family == "on-time":
        name = family_name(family)
        out = clone_replay(replay, inputs)
        sidecar = {
            "schema": 1,
            "family": name,
            "k": 0,
            "clickIndex": None,
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
    if click_index is None:
        click_index = default_click_index(pairs)
    if click_index < 0 or click_index >= len(pairs):
        raise ValueError(f"click_index {click_index} out of range 0..{len(pairs)-1}")
    pair = pairs[click_index]
    name = family_name(family, k)
    original_hold = pair.hold_length

    if family == "drop-click":
        drop = {pair.press_index, pair.release_index}
        kept = [ev for i, ev in enumerate(inputs) if i not in drop]
        out = clone_replay(replay, kept)
        sidecar = {
            "schema": 1,
            "family": name,
            "k": 0,
            "clickIndex": click_index,
            "player2": pair.player2,
            "button": pair.button,
            "shiftedIndices": [],
            "removedIndices": sorted(drop),
            "originalHoldLength": original_hold,
            "newHoldLength": None,
            "clamped": False,
            "goldPressFrame": pair.press_frame,
            "goldReleaseFrame": pair.release_frame,
            "capture": {
                "mode": "pre_would_be_press",
                "priorEventCount": sum(1 for ev in kept if ev.frame < pair.press_frame),
                "captureFrame": pair.press_frame,
                "notes": "drop-click: capture before missing press after prior derived events",
            },
        }
        return ShiftResult(name, 0, click_index, out, sidecar, False)

    if k < 0:
        raise ValueError("k must be >= 0")
    if k == 0:
        # Identity edit of the selected click — must equal gold timeline.
        out = clone_replay(replay, inputs)
        sidecar = {
            "schema": 1,
            "family": name,
            "k": 0,
            "clickIndex": click_index,
            "player2": pair.player2,
            "button": pair.button,
            "shiftedIndices": [pair.press_index, pair.release_index],
            "originalHoldLength": original_hold,
            "newHoldLength": original_hold,
            "clamped": False,
            "goldPressFrame": pair.press_frame,
            "goldReleaseFrame": pair.release_frame,
            "newPressFrame": pair.press_frame,
            "newReleaseFrame": pair.release_frame,
            "capture": {
                "mode": "pre_shifted_press",
                "priorEventCount": pair.press_index,
                "captureFrame": pair.press_frame,
                "notes": "k=0 identity",
            },
        }
        return ShiftResult(name, 0, click_index, out, sidecar, False)

    press_lo, press_hi, release_lo, release_hi = _neighbor_bounds(inputs, pair)
    clamped = False

    if family.endswith("early"):
        new_press, c1 = _clamp(pair.press_frame - k, press_lo, press_hi)
        clamped |= c1
        if family.startswith("pulse-shift"):
            # Preserve hold: release moves by the same delta as the (possibly clamped) press.
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

    inputs[pair.press_index].frame = new_press
    if family.startswith("pulse-shift"):
        inputs[pair.release_index].frame = new_release
    # edge-only leaves release frame unchanged

    new_hold = inputs[pair.release_index].frame - inputs[pair.press_index].frame
    out = clone_replay(replay, inputs)
    # Re-sort for stable same-tick order (frame, player2, button, down)
    out.inputs.sort(key=lambda item: (item.frame, item.player2, item.button, 0 if item.down else 1))

    # After sort, find new indices for sidecar
    press_indices = [
        i
        for i, ev in enumerate(out.inputs)
        if ev.frame == new_press
        and ev.player2 == pair.player2
        and ev.button == pair.button
        and ev.down
    ]
    release_indices = [
        i
        for i, ev in enumerate(out.inputs)
        if ev.frame == (new_release if family.startswith("pulse-shift") else pair.release_frame)
        and ev.player2 == pair.player2
        and ev.button == pair.button
        and not ev.down
    ]
    shifted = []
    if press_indices:
        shifted.append(press_indices[0])
    if family.startswith("pulse-shift") and release_indices:
        shifted.append(release_indices[0])
    elif family.startswith("edge-only") and release_indices:
        shifted.append(release_indices[0])  # unmoved but paired

    prior = sum(1 for ev in out.inputs if ev.frame < new_press or (ev.frame == new_press and not ev.down))
    # Capture immediately before shifted press: count events strictly before the press event.
    if press_indices:
        prior = press_indices[0]

    sidecar = {
        "schema": 1,
        "family": name,
        "baseFamily": family,
        "k": k,
        "clickIndex": click_index,
        "player2": pair.player2,
        "button": pair.button,
        "shiftedIndices": shifted,
        "originalHoldLength": original_hold,
        "newHoldLength": new_hold,
        "clamped": clamped,
        "goldPressFrame": pair.press_frame,
        "goldReleaseFrame": pair.release_frame,
        "newPressFrame": new_press,
        "newReleaseFrame": (
            new_release if family.startswith("pulse-shift") else pair.release_frame
        ),
        "pair": asdict(pair),
        "capture": {
            "mode": "pre_shifted_press",
            "priorEventCount": prior,
            "captureFrame": new_press,
            "notes": (
                "Replay all prior events of this derived file, then capture state "
                "immediately before the shifted press (not gold frame F)."
            ),
        },
    }
    return ShiftResult(name, k, click_index, out, sidecar, clamped)


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
