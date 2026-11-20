"""One-shot report: show real numeric differences across dense synthetic coverage."""

from __future__ import annotations

import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from real_assist.dense import delay_sweep_presses, score_sequence_presses, sequence_presses
from real_assist.oracle import load_json
from real_assist.score import score_session

GOLDENS = ROOT / "fixtures" / "real_assist" / "dense" / "dense_goldens.json"
SMOKE = ROOT / "fixtures" / "real_assist"


def score_row(corpus_id: str, row: dict, limit: int) -> dict:
    physics = {
        "schema": 1,
        "kind": "PhysicsSafeSet",
        "claims": "ground-truth",
        "safeDelays": list(row.get("physicsSafe") or []),
        "status": "ready",
    }
    verifier = {
        "schema": 1,
        "kind": "VerifierSafeSet",
        "claims": "diagnostic",
        "safeDelays": list(row.get("verifierSafe") or []),
        "status": "ready",
    }
    session = {
        "corpusId": corpus_id,
        "variant": row.get("family"),
        "mode": "Live",
        "protocol": "Frozen",
        "attempts": [{"index": 0, "completed": True, "arms": 1, "daf": 0, "nOpp": 1}],
        "presses": delay_sweep_presses(limit),
    }
    scored = score_session(session, physics_oracle=physics, verifier_oracle=verifier)
    return {
        "family": row.get("family"),
        "seq": int(row.get("sequenceLength") or 1),
        "clicks": row.get("clickIndices"),
        "physicsSafe": list(row.get("physicsSafe") or []),
        "verifierSafe": list(row.get("verifierSafe") or []),
        "PHit": scored.PhysicsHitRate,
        "VHit": scored.VerifierHitRate,
        "Invent": scored.InventRate,
        "disagree": list(scored.verifierPhysicsDisagreements),
    }


def main() -> None:
    goldens = load_json(GOLDENS)
    windows = [int(w) for w in goldens["windows"]]
    primary = int(goldens["limit"])
    print("=== DENSE DIFF REPORT (synthetic offline) ===")
    print(f"windows={windows} primary={primary}")
    print()

    # 1) Same family, different k -> different safe centers / PHit on full delay sweep
    print("--- pulse-shift-early by k (slaughterhouse, seq1, L12 delay-sweep) ---")
    level = goldens["levels"]["slaughterhouse"]
    by_k = {}
    for row in level["variants"]:
        if int(row.get("sequenceLength") or 1) != 1:
            continue
        fam = str(row.get("family") or "")
        if not fam.startswith("pulse-shift-early-"):
            continue
        k = int(fam.rsplit("-", 1)[1])
        # one representative click
        if k in by_k:
            continue
        by_k[k] = score_row("slaughterhouse", row, primary)
    for k in sorted(by_k):
        r = by_k[k]
        print(
            f"  k={k}: safe={r['physicsSafe']} PHit={r['PHit']:.3f} "
            f"Invent={r['Invent']:.3f} disagree={r['disagree']}"
        )

    # 2) Same k across windows -> band width / safe clip changes
    print()
    print("--- pulse-shift-early-4 across tick windows (slaughterhouse) ---")
    sample = next(
        r
        for r in level["variants"]
        if r.get("family") == "pulse-shift-early-4" and int(r.get("sequenceLength") or 1) == 1
    )
    for window_row in sample["windows"]:
        limit = int(window_row["limit"])
        safe = list(window_row["physicsSafe"])
        physics = {
            "kind": "PhysicsSafeSet",
            "claims": "ground-truth",
            "safeDelays": safe,
            "status": "ready",
        }
        session = {
            "corpusId": "slaughterhouse",
            "variant": "pulse-shift-early-4",
            "mode": "Live",
            "protocol": "Frozen",
            "attempts": [{"completed": True, "arms": 1, "daf": 0, "nOpp": 1}],
            "presses": delay_sweep_presses(limit),
        }
        scored = score_session(session, physics_oracle=physics, verifier_oracle=None)
        print(
            f"  L{limit}: safe={safe} band={limit + 1} "
            f"PHit={scored.PhysicsHitRate:.3f} Invent={scored.InventRate:.3f}"
        )

    # 3) Family differences at same k
    print()
    print("--- family compare at k=2, same click (slaughterhouse) ---")
    click = sample["clickIndex"]
    for fam_prefix in (
        "pulse-shift-early-2",
        "edge-only-early-2",
        "pulse-shift-late-2",
        "edge-only-late-2",
        "on-time",
        "drop-click",
    ):
        row = next(
            (
                r
                for r in level["variants"]
                if r.get("family") == fam_prefix
                and int(r.get("sequenceLength") or 1) == 1
                and (fam_prefix in ("on-time", "drop-click") or r.get("clickIndex") == click)
            ),
            None,
        )
        if not row:
            # drop-click uses same click
            row = next(
                (
                    r
                    for r in level["variants"]
                    if r.get("family") == fam_prefix and r.get("clickIndex") == click
                ),
                None,
            )
        if not row:
            print(f"  {fam_prefix}: missing")
            continue
        r = score_row("slaughterhouse", row, primary)
        phit = "n/a" if r["PHit"] is None else f"{r['PHit']:.3f}"
        invent = "n/a" if r["Invent"] is None else f"{r['Invent']:.3f}"
        print(
            f"  {fam_prefix}: safe={r['physicsSafe']} PHit={phit} "
            f"Invent={invent} disagree={r['disagree']}"
        )

    # 4) Multi-click sequence: good center vs invent last press
    print()
    print("--- multi-click sequence (slaughterhouse pulse-shift-early-2 seq3) ---")
    seq = next(
        r
        for r in level["variants"]
        if r.get("family") == "pulse-shift-early-2" and int(r.get("sequenceLength") or 1) == 3
    )
    primary_w = next(w for w in seq["windows"] if int(w["limit"]) == primary)
    per_click = list(primary_w["perClickPhysicsSafe"])
    good = sequence_presses(per_click, pick="center")
    bad = [dict(p) for p in good]
    bad[-1] = dict(bad[-1])
    bad[-1]["plannedDelay"] = primary + 1
    g = score_sequence_presses(good)
    b = score_sequence_presses(bad)
    print(f"  clicks={seq.get('clickIndices')} perClickSafe={per_click}")
    print(f"  center picks: PHit={g['PhysicsHitRate']:.3f} Invent={g['InventRate']:.3f}")
    print(f"  last invent:  PHit={b['PhysicsHitRate']:.3f} Invent={b['InventRate']:.3f}")

    # 5) Smoke Live vs Hybrid (should be close synthetically) + invent hard-fail
    print()
    print("--- smoke fixtures (APR3/4) ---")
    from real_assist.score import score_session as ss

    cases = [
        (
            "sessions/slaughterhouse_pulse-shift-early-2_Live_Frozen.json",
            "oracles/slaughterhouse_pulse-shift-early-2.physics_safe.json",
            "oracles/slaughterhouse_pulse-shift-early-2.verifier_safe.json",
        ),
        (
            "sessions/slaughterhouse_pulse-shift-early-2_Hybrid_Frozen.json",
            "oracles/slaughterhouse_pulse-shift-early-2.physics_safe.json",
            "oracles/slaughterhouse_pulse-shift-early-2.verifier_safe.json",
        ),
        (
            "sessions/slaughterhouse_pulse-shift-early-2_Live_invent.json",
            "oracles/slaughterhouse_pulse-shift-early-2.physics_safe.json",
            "oracles/slaughterhouse_pulse-shift-early-2.verifier_safe.json",
        ),
        (
            "sessions/slaughterhouse_on-time_Live_Frozen.json",
            "oracles/slaughterhouse_on-time.physics_safe.json",
            "oracles/slaughterhouse_on-time.verifier_safe.json",
        ),
    ]
    for session_rel, phys_rel, ver_rel in cases:
        scored = ss(
            load_json(SMOKE / session_rel),
            physics_oracle=load_json(SMOKE / phys_rel),
            verifier_oracle=load_json(SMOKE / ver_rel),
        )
        print(
            f"  {Path(session_rel).stem}: Surv={scored.SurviveRate} "
            f"PHit={scored.PhysicsHitRate} Invent={scored.InventRate} "
            f"disagree={scored.verifierPhysicsDisagreements} hardFail={scored.hardFail}"
        )

    # 6) Coverage histogram: unique PHit values across all slaughterhouse primary sweeps
    print()
    print("--- slaughterhouse PHit distribution (all seq1 variants, L12 sweep) ---")
    hist = defaultdict(int)
    for row in level["variants"]:
        if int(row.get("sequenceLength") or 1) != 1:
            continue
        r = score_row("slaughterhouse", row, primary)
        key = "n/a" if r["PHit"] is None else f"{r['PHit']:.3f}"
        hist[key] += 1
    for key in sorted(hist, key=lambda x: (-1 if x == "n/a" else float(x))):
        print(f"  PHit={key}: {hist[key]} variants")

    print()
    print("DONE")


if __name__ == "__main__":
    main()
