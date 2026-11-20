"""APR4: freeze smoke PhysicsSafeSet goldens + gate before AP00 feature work."""

from __future__ import annotations

import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from real_assist.compare_support import build_compare_from_goldens
from real_assist.oracle import load_json, write_json
from real_assist.score import score_session

FIXTURE = ROOT / "fixtures" / "real_assist"
SMOKE = FIXTURE / "smoke_goldens.json"
BASELINE = REPO / "docs" / "dev" / "real_assist_baseline.md"

REQUIRED_SMOKE = [
    "cataclysm/pulse-shift-early-2",
    "hoveropolis/pulse-shift-early-2",
    "slaughterhouse/pulse-shift-early-2",
    "quantum/on-time",
]


def _nearly(actual: float | None, expected: float | None, eps: float = 1e-9) -> bool:
    if actual is None and expected is None:
        return True
    if actual is None or expected is None:
        return False
    return abs(float(actual) - float(expected)) <= eps


def _variant_from_key(smoke_key: str) -> str:
    return smoke_key.split("/", 1)[1]


def freeze() -> None:
    goldens = load_json(SMOKE)
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

    floors: dict[str, float] = {}
    for case in goldens["cases"]:
        session = load_json(FIXTURE / case["session"])
        if session.get("variant") != "on-time" or session.get("mode") != "Live":
            continue
        if session.get("protocol") != "Frozen":
            continue
        key = (
            f"{session['corpusId']}/{session['variant']}/"
            f"{session['mode']}/{session['protocol']}"
        )
        floors[key] = float(case["expect"]["SurviveRate"])
    goldens["onTimeSurviveFloor"] = floors
    goldens["frozenAt"] = (
        datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    )
    write_json(SMOKE, goldens)
    write_baseline(goldens)
    print(f"froze {SMOKE}")
    print(f"wrote {BASELINE}")


def write_baseline(goldens: dict | None = None) -> None:
    goldens = goldens or load_json(SMOKE)
    compare = build_compare_from_goldens(FIXTURE, goldens)
    frozen = (compare.get("sections") or {}).get("Frozen") or {}
    lines = [
        "# Real Assist baseline (APR4)",
        "",
        "Gate before **AP00** feature work. Acceptance metric is **PhysicsHitRate**; "
        "**VerifierHitRate** is diagnostic only.",
        "",
        f"- Frozen at: `{goldens.get('frozenAt') or 'see smoke_goldens.json'}`",
        "- Protocol: **Frozen** only (Online is a separate ColdStart lane)",
        "- Fixtures: synthetic until each corpus `reproduce.json` is `pass` and "
        "PlayLayer fills real PhysicsSafeSet bands",
        "",
        "## Dual honesty",
        "",
        (goldens.get("dualHonesty") or {}).get("note")
        or "Dual corpora graded Live vs Hybrid fairly; no Hybrid/AI-first shortcut.",
        "",
        "## Labeling: pulse-shift vs edge-only",
        "",
        f"- Pulse-shift: {(goldens.get('labeling') or {}).get('pulseShift')}",
        f"- Edge-only: {(goldens.get('labeling') or {}).get('edgeOnly')}",
        "- Smoke subset uses `pulse-shift-early-2` (and `quantum/on-time`) - never alias edge-only.",
        "",
        "## Smoke keys",
        "",
    ]
    for key in goldens.get("smokeKeys") or REQUIRED_SMOKE:
        lines.append(f"- `{key}`")
    lines += [
        "",
        "## Live vs Hybrid (Frozen)",
        "",
        "| corpus | variant | Live Survive | Live PhysicsHit | Hybrid Survive | Hybrid PhysicsHit | d PhysicsHit |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for entry in frozen.get("cases") or []:
        live = entry.get("Live") or {}
        hybrid = entry.get("Hybrid") or {}
        delta = entry.get("physicsHitDelta_HybridMinusLive")
        lines.append(
            "| {corpus} | {variant} | {ls} | {lp} | {hs} | {hp} | {d} |".format(
                corpus=entry.get("corpusId"),
                variant=entry.get("variant"),
                ls=live.get("SurviveRate"),
                lp=live.get("PhysicsHitRate"),
                hs=hybrid.get("SurviveRate"),
                hp=hybrid.get("PhysicsHitRate"),
                d=delta if delta is not None else "",
            )
        )
    lines += [
        "",
        "## Gate commands",
        "",
        "```bat",
        "python tools/test_real_assist_score.py",
        "python tools/test_real_assist_smoke.py",
        "python tools/test_real_assist_smoke.py --freeze",
        "python tools/run_live_regression_ap07.py",
        "```",
        "",
        "Hard-fail: `InventRate > 0`, on-time SurviveRate below frozen floor, missing smoke oracle, "
        "or Physics/Verifier claim/kind mismatch.",
        "",
        "## Dense coverage (not just smoke)",
        "",
        "Smoke (4 keys) is the AP00 gate only. Assist quality needs multi-click x shift x delay bands:",
        "",
        "```bat",
        "python tools/test_real_assist_dense.py --freeze",
        "python tools/test_real_assist_dense.py",
        "```",
        "",
        "Default: `quantile:8` clicks/level, families x `k=1..8`, delay `0..12` "
        "(~3400 delay probes / ~770 synthetic safe points per level).",
        "",
        "## AP00 rule",
        "",
        "Do not start AP00 until smoke is green. Keep dense green for meaningful assist results.",
        "",
    ]
    BASELINE.parent.mkdir(parents=True, exist_ok=True)
    BASELINE.write_text("\n".join(lines), encoding="utf-8")


def run() -> None:
    goldens = load_json(SMOKE)
    fails = 0

    smoke_keys = list(goldens.get("smokeKeys") or [])
    for required in REQUIRED_SMOKE:
        if required not in smoke_keys:
            print(f"FAIL missing smoke key {required}")
            fails += 1

    covered = {case.get("smokeKey") for case in goldens.get("cases") or []}
    for required in REQUIRED_SMOKE:
        if required not in covered:
            print(f"FAIL no cases for smoke key {required}")
            fails += 1

    seen_oracle: set[str] = set()
    for case in goldens.get("cases") or []:
        physics_path = FIXTURE / case["physics"]
        verifier_path = FIXTURE / case["verifier"]
        session_path = FIXTURE / case["session"]
        for path, label in (
            (physics_path, "physics"),
            (verifier_path, "verifier"),
            (session_path, "session"),
        ):
            if not path.is_file():
                print(f"FAIL {case['name']} missing {label} {path.name}")
                fails += 1

        physics = load_json(physics_path)
        verifier = load_json(verifier_path)
        session = load_json(session_path)
        smoke_key = case.get("smokeKey") or ""
        want_variant = _variant_from_key(smoke_key) if "/" in smoke_key else ""

        if physics.get("kind") != "PhysicsSafeSet" or physics.get("claims") != "ground-truth":
            print(f"FAIL {case['name']} physics kind/claims")
            fails += 1
        if verifier.get("kind") != "VerifierSafeSet" or verifier.get("claims") != "diagnostic":
            print(f"FAIL {case['name']} verifier kind/claims")
            fails += 1
        if physics.get("family") != want_variant:
            print(
                f"FAIL {case['name']} physics family={physics.get('family')} "
                f"want {want_variant}"
            )
            fails += 1
        if verifier.get("family") != want_variant:
            print(
                f"FAIL {case['name']} verifier family={verifier.get('family')} "
                f"want {want_variant}"
            )
            fails += 1
        if "edge-only" in str(physics.get("family") or "") or "edge-only" in smoke_key:
            print(f"FAIL {case['name']} edge-only must not be smoke-aliased as pulse")
            fails += 1
        if session.get("variant") != want_variant:
            print(
                f"FAIL {case['name']} session variant={session.get('variant')} "
                f"want {want_variant}"
            )
            fails += 1
        if session.get("protocol") != "Frozen":
            print(f"FAIL {case['name']} protocol must be Frozen for smoke baseline")
            fails += 1

        oracle_id = f"{physics_path.name}|{verifier_path.name}"
        if oracle_id not in seen_oracle:
            seen_oracle.add(oracle_id)
            print(
                f"OK oracle {smoke_key} physics={physics.get('safeDelays')} "
                f"verifier={verifier.get('safeDelays')}"
            )

        scored = score_session(session, physics_oracle=physics, verifier_oracle=verifier)
        expect = case.get("expect") or {}
        if not expect:
            print(f"FAIL {case['name']} empty expect — run with --freeze first")
            fails += 1
            continue

        ok = True
        for label, actual, expected in (
            ("SurviveRate", scored.SurviveRate, expect.get("SurviveRate")),
            ("PhysicsHitRate", scored.PhysicsHitRate, expect.get("PhysicsHitRate")),
            ("VerifierHitRate", scored.VerifierHitRate, expect.get("VerifierHitRate")),
            ("InventRate", scored.InventRate, expect.get("InventRate")),
        ):
            if expected is None:
                continue
            if not _nearly(actual, expected):
                print(f"FAIL {case['name']} {label}: {actual} != {expected}")
                ok = False
        if bool(scored.hardFail) != bool(expect.get("hardFail")):
            print(f"FAIL {case['name']} hardFail: {scored.hardFail} != {expect.get('hardFail')}")
            ok = False
        if len(scored.verifierPhysicsDisagreements) != int(expect.get("disagreementCount", 0)):
            print(
                f"FAIL {case['name']} disagreements: "
                f"{scored.verifierPhysicsDisagreements} count!="
                f"{expect.get('disagreementCount')}"
            )
            ok = False
        if scored.acceptanceUses != "PhysicsHitRate":
            print(f"FAIL {case['name']} acceptanceUses={scored.acceptanceUses}")
            ok = False
        if scored.InventRate is not None and scored.InventRate > 0:
            print(f"FAIL {case['name']} InventRate>0 hard-fail")
            ok = False
        if ok:
            print(
                f"OK {case['name']} Surv={scored.SurviveRate} "
                f"PHit={scored.PhysicsHitRate} Invent={scored.InventRate}"
            )
        else:
            fails += 1

    for key, floor in (goldens.get("onTimeSurviveFloor") or {}).items():
        if floor is None:
            print(f"FAIL on-time floor {key} unset — run --freeze")
            fails += 1
            continue
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

    compare = build_compare_from_goldens(FIXTURE, goldens)
    frozen_cases = ((compare.get("sections") or {}).get("Frozen") or {}).get("cases") or []
    for required in REQUIRED_SMOKE:
        corpus, variant = required.split("/", 1)
        entry = next(
            (
                c
                for c in frozen_cases
                if c.get("corpusId") == corpus and c.get("variant") == variant
            ),
            None,
        )
        if not entry or not entry.get("Live") or not entry.get("Hybrid"):
            print(f"FAIL compare missing Live/Hybrid for {required}")
            fails += 1
        else:
            print(f"OK compare Frozen {required} Live/Hybrid present")

    if "Online" in (compare.get("sections") or {}):
        print("FAIL Online mixed into smoke baseline (must stay separate)")
        fails += 1

    if not BASELINE.is_file():
        print(f"FAIL missing baseline doc {BASELINE}")
        fails += 1
    else:
        text = BASELINE.read_text(encoding="utf-8")
        missing = [
            needle
            for needle in ("PhysicsHitRate", "pulse-shift", "edge-only", "AP00")
            if needle not in text
        ]
        if missing:
            for needle in missing:
                print(f"FAIL baseline missing '{needle}'")
            fails += len(missing)
        else:
            print(f"OK baseline doc {BASELINE.name}")

    # Keep APR3 score goldens green when smoke is the regression entrypoint.
    nested = subprocess.run(
        [sys.executable, str(ROOT / "test_real_assist_score.py")],
        cwd=str(REPO),
        capture_output=True,
        text=True,
    )
    if nested.returncode != 0:
        print(nested.stdout)
        print(nested.stderr)
        print(f"FAIL nested APR3 score goldens exit={nested.returncode}")
        fails += 1
    else:
        print("OK nested APR3 score goldens")

    if fails:
        raise SystemExit(f"Real assist smoke gate: {fails} fails")
    print("Real assist smoke gate OK - APR4 green before AP00.")


if __name__ == "__main__":
    if "--freeze" in sys.argv:
        freeze()
    run()
