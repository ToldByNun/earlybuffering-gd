"""APR3 CI: score goldens, InventRate hard-fail, on-time SurviveRate floor, mode compare."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from real_assist.compare_support import build_compare_from_goldens
from real_assist.oracle import load_json, write_json
from real_assist.score import score_session

FIXTURE = ROOT / "fixtures" / "real_assist"
GOLDENS = FIXTURE / "score_goldens.json"


def _nearly(actual: float | None, expected: float | None, eps: float = 1e-9) -> bool:
    if actual is None and expected is None:
        return True
    if actual is None or expected is None:
        return False
    return abs(float(actual) - float(expected)) <= eps


def freeze() -> None:
    """Recompute expect blocks from current synthetic sessions + oracles."""
    goldens = load_json(GOLDENS)
    for case in goldens["cases"]:
        session = load_json(FIXTURE / case["session"])
        physics = load_json(FIXTURE / case["physics"])
        verifier = load_json(FIXTURE / case["verifier"])
        scored = score_session(session, physics_oracle=physics, verifier_oracle=verifier)
        case["expect"] = {
            "SurviveRate": scored.SurviveRate,
            "PhysicsHitRate": scored.PhysicsHitRate,
            "VerifierHitRate": scored.VerifierHitRate,
            "InventRate": scored.InventRate,
            "hardFail": scored.hardFail,
            "disagreementCount": len(scored.verifierPhysicsDisagreements),
        }
    # On-time Survive floor from on-time Live Frozen case.
    floors = {}
    for case in goldens["cases"]:
        if "on-time" in case["name"] and "live" in case["name"]:
            key = "slaughterhouse/on-time/Live/Frozen"
            floors[key] = case["expect"]["SurviveRate"]
    goldens["onTimeSurviveFloor"] = floors
    write_json(GOLDENS, goldens)
    print(f"froze {GOLDENS}")


def run() -> None:
    goldens = load_json(GOLDENS)
    fails = 0
    for case in goldens["cases"]:
        session = load_json(FIXTURE / case["session"])
        physics = load_json(FIXTURE / case["physics"])
        verifier = load_json(FIXTURE / case["verifier"])
        scored = score_session(session, physics_oracle=physics, verifier_oracle=verifier)
        expect = case["expect"]
        checks = [
            ("SurviveRate", scored.SurviveRate, expect.get("SurviveRate")),
            ("PhysicsHitRate", scored.PhysicsHitRate, expect.get("PhysicsHitRate")),
            ("VerifierHitRate", scored.VerifierHitRate, expect.get("VerifierHitRate")),
            ("InventRate", scored.InventRate, expect.get("InventRate")),
        ]
        ok = True
        for label, actual, expected in checks:
            if expected is None:
                continue
            if not _nearly(actual, expected):
                print(f"FAIL {case['name']} {label}: {actual} != {expected}")
                ok = False
        if bool(scored.hardFail) != bool(expect.get("hardFail")):
            print(
                f"FAIL {case['name']} hardFail: {scored.hardFail} != {expect.get('hardFail')}"
            )
            ok = False
        if len(scored.verifierPhysicsDisagreements) != int(expect.get("disagreementCount", 0)):
            print(
                f"FAIL {case['name']} disagreements: "
                f"{scored.verifierPhysicsDisagreements} count!="
                f"{expect.get('disagreementCount')}"
            )
            ok = False
        # Acceptance must never key off VerifierHitRate.
        if scored.acceptanceUses != "PhysicsHitRate":
            print(f"FAIL {case['name']} acceptanceUses={scored.acceptanceUses}")
            ok = False
        if expect.get("hardFail") and scored.InventRate is not None and scored.InventRate <= 0:
            print(f"FAIL {case['name']} expected invent hard-fail")
            ok = False
        if not expect.get("hardFail") and scored.InventRate is not None and scored.InventRate > 0:
            print(f"FAIL {case['name']} InventRate>0 without hardFail")
            ok = False
        if ok:
            print(
                f"OK {case['name']} Surv={scored.SurviveRate} "
                f"PHit={scored.PhysicsHitRate} Invent={scored.InventRate} "
                f"disagree={scored.verifierPhysicsDisagreements}"
            )
        else:
            fails += 1

    # On-time SurviveRate regression floor.
    for key, floor in (goldens.get("onTimeSurviveFloor") or {}).items():
        corpus, variant, mode, protocol = key.split("/")
        matched = None
        for case in goldens["cases"]:
            session = load_json(FIXTURE / case["session"])
            if (
                session.get("corpusId") == corpus
                and session.get("variant") == variant
                and session.get("mode") == mode
                and session.get("protocol") == protocol
            ):
                physics = load_json(FIXTURE / case["physics"])
                verifier = load_json(FIXTURE / case["verifier"])
                matched = score_session(
                    session, physics_oracle=physics, verifier_oracle=verifier
                )
                break
        if matched is None:
            print(f"FAIL missing on-time floor session {key}")
            fails += 1
            continue
        if matched.SurviveRate is None or matched.SurviveRate + 1e-9 < float(floor):
            print(
                f"FAIL on-time SurviveRate regression {key}: "
                f"{matched.SurviveRate} < floor {floor}"
            )
            fails += 1
        else:
            print(f"OK on-time Survive floor {key} >= {floor}")

    # Mode compare: Live vs Hybrid separated under Frozen.
    compare = build_compare_from_goldens(FIXTURE, goldens)
    if "Frozen" not in compare["sections"]:
        print("FAIL compare missing Frozen section")
        fails += 1
    else:
        frozen_cases = compare["sections"]["Frozen"]["cases"]
        pulse = next(
            (
                c
                for c in frozen_cases
                if c["corpusId"] == "slaughterhouse" and c["variant"] == "pulse-shift-early-2"
            ),
            None,
        )
        if not pulse or not pulse.get("Live") or not pulse.get("Hybrid"):
            print("FAIL compare missing Live/Hybrid pulse pair")
            fails += 1
        else:
            print(
                "OK compare Frozen pulse Live/Hybrid "
                f"PHit delta={pulse.get('physicsHitDelta_HybridMinusLive')}"
            )
    if "Online" in compare["sections"]:
        # Synthetic goldens are Frozen-only; Online must stay a separate map if present.
        print("OK Online section isolated")

    if fails:
        raise SystemExit(f"Real assist score goldens: {fails} fails")
    print("Real assist score goldens OK.")


if __name__ == "__main__":
    if "--freeze" in sys.argv:
        freeze()
    run()
