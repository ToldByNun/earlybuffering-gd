"""APR1 smoke: k=0 == gold, no illegal crossings, pulse holds, edge lengthens."""

from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from real_assist.gdr2 import Gdr2Input, Gdr2Replay, export_gdr2, parse_gdr2
from real_assist.oracle import require_reproduce_pass
from real_assist.shift import apply_multi_shift, apply_shift, events_equal, pair_clicks


def _toy_replay() -> Gdr2Replay:
    # P1 clicks at (10,20) hold=10 and (40,50) hold=10; P2 (15,25).
    inputs = [
        Gdr2Input(10, 1, False, True),
        Gdr2Input(15, 1, True, True),
        Gdr2Input(20, 1, False, False),
        Gdr2Input(25, 1, True, False),
        Gdr2Input(40, 1, False, True),
        Gdr2Input(50, 1, False, False),
    ]
    return Gdr2Replay(
        version=2,
        input_tag="",
        author="test",
        description="",
        duration=0.0,
        game_version=22074,
        framerate=240.0,
        seed=0,
        coins=0,
        ldm=False,
        platformer=False,
        bot_name="MEGA",
        bot_version=1,
        level_id=0,
        level_name="toy",
        deaths=[],
        inputs=inputs,
    )


def test_pair_and_k0() -> None:
    replay = _toy_replay()
    pairs = pair_clicks(replay.inputs)
    assert len(pairs) == 3, pairs
    identity = apply_shift(replay, "pulse-shift-early", k=0, click_index=0)
    assert events_equal(identity.replay.inputs, replay.inputs)
    on_time = apply_shift(replay, "on-time")
    assert events_equal(on_time.replay.inputs, replay.inputs)


def test_pulse_preserves_hold() -> None:
    replay = _toy_replay()
    result = apply_shift(replay, "pulse-shift-early", k=2, click_index=0)
    assert result.sidecar["originalHoldLength"] == 10
    assert result.sidecar["newHoldLength"] == 10
    assert result.sidecar["newPressFrame"] == 8
    assert result.sidecar["newReleaseFrame"] == 18
    assert not result.clamped


def test_edge_only_lengthens() -> None:
    replay = _toy_replay()
    result = apply_shift(replay, "edge-only-early", k=2, click_index=0)
    assert result.sidecar["originalHoldLength"] == 10
    assert result.sidecar["newHoldLength"] == 12
    assert result.sidecar["newPressFrame"] == 8
    assert result.sidecar["newReleaseFrame"] == 20


def test_no_neighbor_cross() -> None:
    replay = _toy_replay()
    # Second P1 click early by a huge k must clamp above previous release (20).
    result = apply_shift(replay, "pulse-shift-early", k=30, click_index=2)
    assert result.clamped
    assert result.sidecar["newPressFrame"] >= 21
    frames = [ev.frame for ev in result.replay.inputs if not ev.player2]
    assert frames == sorted(frames)


def test_drop_click() -> None:
    replay = _toy_replay()
    result = apply_shift(replay, "drop-click", click_index=0)
    assert len(result.replay.inputs) == len(replay.inputs) - 2
    assert all(not (ev.frame == 10 and ev.down and not ev.player2) for ev in result.replay.inputs)


def test_multi_click_sequence() -> None:
    replay = _toy_replay()
    # P1 pairs are click indices 0 and 2 in pair_clicks order (P2 is 1).
    pairs = pair_clicks(replay.inputs)
    p1 = [i for i, pair in enumerate(pairs) if not pair.player2]
    assert len(p1) >= 2
    result = apply_multi_shift(
        replay, "pulse-shift-early", k=2, click_indices=[p1[0], p1[1]]
    )
    assert result.sidecar["sequenceLength"] == 2
    assert result.sidecar["clickIndices"] == [p1[0], p1[1]]
    assert len(result.sidecar["edits"]) == 2
    # Both holds preserved under pulse-shift.
    for edit in result.sidecar["edits"]:
        assert edit["newHoldLength"] == edit["originalHoldLength"]


def test_roundtrip_export() -> None:
    replay = _toy_replay()
    shifted = apply_shift(replay, "pulse-shift-early", k=1, click_index=0)
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "x.gdr2"
        export_gdr2(shifted.replay, path)
        loaded = parse_gdr2(path)
        assert events_equal(loaded.inputs, shifted.replay.inputs)
        assert loaded.framerate == 240.0


def test_oracle_refuses_without_pass() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        folder = Path(tmp)
        (folder / "reproduce.json").write_text(
            json.dumps({"status": "pending"}) + "\n", encoding="utf-8"
        )
        try:
            require_reproduce_pass(folder)
            raise AssertionError("expected refuse")
        except RuntimeError as exc:
            assert "reproduce=pending" in str(exc)


def test_corpus_smoke_if_present() -> None:
    clear = ROOT / "fixtures" / "real_corpus" / "slaughterhouse" / "clear.gdr2"
    if not clear.is_file():
        print("skip corpus smoke (no clear.gdr2)")
        return
    gold = parse_gdr2(clear)
    k0 = apply_shift(gold, "pulse-shift-early", k=0, click_index=0)
    assert events_equal(k0.replay.inputs, gold.inputs)
    pulse = apply_shift(gold, "pulse-shift-early", k=2)
    edge = apply_shift(gold, "edge-only-early", k=2, click_index=pulse.click_index)
    assert pulse.sidecar["newHoldLength"] == pulse.sidecar["originalHoldLength"]
    assert edge.sidecar["newHoldLength"] >= edge.sidecar["originalHoldLength"]
    print(
        f"corpus slaughterhouse click={pulse.click_index} "
        f"pulseHold={pulse.sidecar['newHoldLength']} "
        f"edgeHold={edge.sidecar['newHoldLength']}"
    )


def run() -> None:
    test_pair_and_k0()
    test_pulse_preserves_hold()
    test_edge_only_lengthens()
    test_no_neighbor_cross()
    test_drop_click()
    test_multi_click_sequence()
    test_roundtrip_export()
    test_oracle_refuses_without_pass()
    test_corpus_smoke_if_present()
    print("Shift synthesizer smoke OK.")


if __name__ == "__main__":
    run()
