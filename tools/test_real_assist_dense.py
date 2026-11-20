"""Dense coverage gate: multi-click sequences × tick windows × delay bands."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from real_assist.dense import (
    delay_sweep_presses,
    full_band,
    score_sequence_presses,
    sequence_presses,
)
from real_assist.oracle import load_json
from real_assist.score import score_session

DENSE_ROOT = ROOT / "fixtures" / "real_assist" / "dense"
GOLDENS = DENSE_ROOT / "dense_goldens.json"


def _oracle_from_safe(
    corpus_id: str, family: str, safe: list[int], *, kind: str, limit: int
) -> dict:
    return {
        "schema": 1,
        "kind": kind,
        "claims": "ground-truth" if kind == "PhysicsSafeSet" else "diagnostic",
        "corpusId": corpus_id,
        "status": "ready",
        "family": family,
        "limit": limit,
        "safeDelays": list(safe),
        "band": full_band(safe, limit),
    }


def freeze() -> None:
    ran = subprocess.run(
        [sys.executable, str(ROOT / "build_dense_safe_fixtures.py"), "--ensure-derived"],
        cwd=str(REPO),
    )
    if ran.returncode != 0:
        raise SystemExit(f"dense freeze failed: {ran.returncode}")


def run() -> None:
    if not GOLDENS.is_file():
        raise SystemExit(f"missing {GOLDENS}; run: python tools/test_real_assist_dense.py --freeze")

    goldens = load_json(GOLDENS)
    fails = 0
    min_probes = int(goldens.get("minDelayProbesPerLevel") or 200)
    min_safe = int(goldens.get("minSafePointsPerLevel") or 100)
    windows = [int(w) for w in (goldens.get("windows") or [12])]
    primary_limit = int(goldens.get("limit") or 12)
    min_seq = goldens.get("minSequenceVariants") or {"2": 1, "3": 1}

    levels = goldens.get("levels") or {}
    if len(levels) < 4:
        print(f"FAIL expected 4 levels, got {len(levels)}")
        fails += 1

    if sorted(windows) != sorted(set(windows)) or not windows:
        print(f"FAIL bad windows {windows}")
        fails += 1
    else:
        print(f"OK windows={windows} primary={primary_limit}")

    total_probes = 0
    total_safe = 0
    for corpus_id, level in sorted(levels.items()):
        probes = int(level.get("delayProbeCount") or 0)
        safe_points = int(level.get("safePointCount") or 0)
        variants = level.get("variants") or []
        seq_counts = {int(k): int(v) for k, v in (level.get("sequenceCounts") or {}).items()}
        total_probes += probes
        total_safe += safe_points

        if probes < min_probes:
            print(f"FAIL {corpus_id} delayProbes={probes} < {min_probes}")
            fails += 1
        else:
            print(f"OK {corpus_id} delayProbes={probes} (>= {min_probes})")
        if safe_points < min_safe:
            print(f"FAIL {corpus_id} safePoints={safe_points} < {min_safe}")
            fails += 1
        else:
            print(f"OK {corpus_id} safePoints={safe_points} (>= {min_safe})")

        for seq_key, need in min_seq.items():
            length = int(seq_key)
            have = seq_counts.get(length, 0)
            if have < int(need):
                print(f"FAIL {corpus_id} sequenceLength={length} count={have} < {need}")
                fails += 1
            else:
                print(f"OK {corpus_id} sequenceLength={length} count={have}")

        level_windows = [int(w) for w in (level.get("windows") or [])]
        if sorted(level_windows) != sorted(windows):
            print(f"FAIL {corpus_id} windows={level_windows} want {windows}")
            fails += 1

        # Delay sweep on primary window for a few single-click variants.
        checked_sweep = 0
        for row in variants:
            if int(row.get("sequenceLength") or 1) != 1:
                continue
            family = str(row.get("family") or "")
            if "pulse-shift-early" not in family and family != "on-time":
                continue
            physics = _oracle_from_safe(
                corpus_id, family, list(row.get("physicsSafe") or []),
                kind="PhysicsSafeSet", limit=primary_limit,
            )
            verifier = _oracle_from_safe(
                corpus_id, family, list(row.get("verifierSafe") or []),
                kind="VerifierSafeSet", limit=primary_limit,
            )
            session = {
                "schema": 1,
                "corpusId": corpus_id,
                "variant": family,
                "mode": "Live",
                "protocol": "Frozen",
                "attempts": [
                    {
                        "index": 0,
                        "completed": True,
                        "endPercent": 100.0,
                        "arms": primary_limit + 1,
                        "daf": 0,
                        "nOpp": primary_limit + 1,
                        "budgetHitRate": 0.0,
                        "avgPlanUs": 1000.0,
                        "assistTouched": True,
                    }
                ],
                "presses": delay_sweep_presses(primary_limit),
            }
            scored = score_session(session, physics_oracle=physics, verifier_oracle=verifier)
            safe = set(physics.get("safeDelays") or [])
            expected_hit = len(safe) / (primary_limit + 1)
            if abs((scored.PhysicsHitRate or -1) - expected_hit) > 1e-9:
                print(f"FAIL {corpus_id}/{family} PHit={scored.PhysicsHitRate} want {expected_hit}")
                fails += 1
            checked_sweep += 1
            if checked_sweep >= 2:
                break

        # Multi-window bands must clip to each limit.
        checked_windows = 0
        for row in variants:
            family = str(row.get("family") or "")
            if "pulse-shift-early" not in family:
                continue
            for window_row in row.get("windows") or []:
                limit = int(window_row["limit"])
                safe = [int(x) for x in (window_row.get("physicsSafe") or [])]
                if any(d > limit for d in safe):
                    print(f"FAIL {corpus_id}/{family} L{limit} safe has d>limit: {safe}")
                    fails += 1
                band = full_band(safe, limit)
                if len(band) != limit + 1:
                    print(f"FAIL {corpus_id}/{family} L{limit} band len={len(band)}")
                    fails += 1
                checked_windows += 1
            if checked_windows >= len(windows):
                break
        if checked_windows < len(windows):
            print(f"FAIL {corpus_id} window coverage checks={checked_windows}")
            fails += 1
        else:
            print(f"OK {corpus_id} multi-window bands checked")

        # Multi-click sequence scoring (presses hintereinander).
        checked_seq = 0
        for row in variants:
            length = int(row.get("sequenceLength") or 1)
            if length < 2:
                continue
            family = str(row.get("family") or "")
            primary = next(
                (w for w in (row.get("windows") or []) if int(w.get("limit")) == primary_limit),
                None,
            )
            if primary is None:
                continue
            per_click = list(primary.get("perClickPhysicsSafe") or [])
            if len(per_click) < length:
                print(f"FAIL {corpus_id}/{family} seq{length} missing perClickPhysicsSafe")
                fails += 1
                continue
            presses = sequence_presses(per_click[:length], pick="center")
            scored = score_sequence_presses(presses)
            if scored["presses"] != float(length):
                print(f"FAIL {corpus_id}/{family} seq press count")
                fails += 1
            if scored["InventRate"] != 0.0:
                print(
                    f"FAIL {corpus_id}/{family} seq InventRate={scored['InventRate']} "
                    f"(center picks must be physics-safe)"
                )
                fails += 1
            # Invent path: force an out-of-band delay on the last press.
            bad = list(presses)
            bad[-1] = dict(bad[-1])
            bad[-1]["plannedDelay"] = primary_limit  # often unsafe for early-k centers
            if bad[-1]["plannedDelay"] in set(bad[-1].get("physicsSafe") or []):
                bad[-1]["plannedDelay"] = primary_limit + 1  # guaranteed invent
                bad[-1]["physicsSafe"] = list(bad[-1].get("physicsSafe") or [])
            invent = score_sequence_presses(bad)
            if invent["InventRate"] <= 0:
                print(f"FAIL {corpus_id}/{family} seq invent path not detected")
                fails += 1
            checked_seq += 1
            if checked_seq >= 2:
                break
        if checked_seq == 0:
            print(f"FAIL {corpus_id} no multi-click sequences to score")
            fails += 1
        else:
            print(f"OK {corpus_id} multi-click sequences scored ({checked_seq})")

    print(
        f"dense totals: levels={len(levels)} delayProbes={total_probes} "
        f"safePoints={total_safe} windows={windows}"
    )
    if total_probes < min_probes * 4:
        print(f"FAIL total delayProbes {total_probes} < {min_probes * 4}")
        fails += 1

    if fails:
        raise SystemExit(f"Real assist dense gate: {fails} fails")
    print("Real assist dense gate OK.")


if __name__ == "__main__":
    if "--freeze" in sys.argv:
        freeze()
    run()
