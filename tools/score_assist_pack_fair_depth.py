#!/usr/bin/env python3
"""Score EarlyBuffer Research Packs for Assist Pack AP1 (fair depth) KPIs.

Baseline pack research_20261113_001322 documents the A4 miss:
  pass-hopeless + later Alive short ticks as not-viable while sealed bestSurv=10.

After AP1 + a new in-game Research Pack, re-run this script on BOTH packs:
  python tools/score_assist_pack_fair_depth.py \\
    --baseline <old_pack_or_session.json> \\
    --candidate <new_pack_or_session.json>

Exit 0 when candidate improves (or ties) every primary KPI vs baseline.
Without --candidate, prints a single-pack report (useful for freezing baseline).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def load_session(path: Path) -> dict[str, Any]:
    p = path
    if p.is_dir():
        session = p / "session.json"
        if not session.is_file():
            matches = sorted(p.glob("session*.json"))
            if not matches:
                raise FileNotFoundError(f"no session.json under {p}")
            session = matches[0]
        p = session
    return json.loads(p.read_text(encoding="utf-8"))


def _is_alive_short(note: dict[str, Any], death_floor: int | None) -> bool:
    if note.get("deathTick", -1) not in (-1, None) and int(note.get("deathTick", -1)) >= 0:
        return False
    ticks = int(note.get("ticksRan") or 0)
    if ticks <= 0:
        return False
    if death_floor is not None and death_floor > 0:
        return ticks < death_floor
    # Fallback: short of common screen / horizon bins.
    return ticks < 48


def score_session(data: dict[str, Any]) -> dict[str, Any]:
    pf_down = 0
    hopeless = 0
    hopeless_with_alive_short = 0
    not_viable_alive_short = 0
    budget_not_reached_alive_short = 0
    sealed = 0
    underselect_zero = 0  # planned=0, bestSurv>0
    underselect_any = 0  # planned < bestSurv (both numeric)
    caused = 0
    # Assist Pack AP2 — Screen→Verify steal
    budget_hit = 0
    budget_hit_with_survivors = 0
    stolen_sum = 0
    stolen_presses = 0
    surv_presses = 0
    # Assist Pack AP3 — weak sticky d0-safe
    d0_safe = 0
    d0_safe_with_underselect = 0  # pass-d0-safe on attempt with planned=0, bestSurv>0

    for att in data.get("attempts") or []:
        ev = att.get("eval") or {}
        att_underselect_zero = False
        if ev.get("valid"):
            sealed += 1
            planned = ev.get("plannedDelay")
            best = ev.get("bestSurvivingDelay")
            if isinstance(planned, int) and isinstance(best, int):
                if planned < best:
                    underselect_any += 1
                if planned == 0 and best > 0:
                    underselect_zero += 1
                    att_underselect_zero = True
            if str(ev.get("simNetClass") or "").lower() == "caused":
                caused += 1

        for evn in att.get("events") or []:
            if evn.get("kind") != "planFinished" or not evn.get("down"):
                continue
            pf_down += 1
            reason = str(evn.get("assistReason") or "")
            if reason.startswith("pass-d0-safe"):
                d0_safe += 1
                if att_underselect_zero:
                    d0_safe_with_underselect += 1
            notes = evn.get("candNotes") or []
            survivors = int(evn.get("adaptiveSurvivors") or 0)
            stolen = int(evn.get("adaptiveStolenScreenTicks") or 0)
            if survivors > 0:
                surv_presses += 1
            if stolen > 0:
                stolen_presses += 1
                stolen_sum += stolen
            if evn.get("budgetHit"):
                budget_hit += 1
                if survivors > 0:
                    budget_hit_with_survivors += 1
            d0 = next((n for n in notes if int(n.get("delay") or -1) == 0), None)
            death_floor = None
            if d0 is not None and int(d0.get("deathTick") or -1) > 0:
                death_floor = int(d0["deathTick"])

            alive_short_notes = [
                n
                for n in notes
                if int(n.get("delay") or -1) > 0 and _is_alive_short(n, death_floor)
            ]
            for n in alive_short_notes:
                code = str(n.get("rejectCode") or "")
                if code == "not-viable":
                    not_viable_alive_short += 1
                elif code == "budget-not-reached":
                    budget_not_reached_alive_short += 1

            if reason.startswith("pass-hopeless"):
                hopeless += 1
                if alive_short_notes:
                    hopeless_with_alive_short += 1

    def rate(n: int, d: int) -> float | None:
        return (n / d) if d > 0 else None

    return {
        "planFinishedDown": pf_down,
        "hopeless": hopeless,
        "hopelessWithAliveShort": hopeless_with_alive_short,
        "hopelessWithAliveShortRate": rate(hopeless_with_alive_short, max(1, hopeless)),
        "notViableAliveShort": not_viable_alive_short,
        "budgetNotReachedAliveShort": budget_not_reached_alive_short,
        "sealedAttempts": sealed,
        "underselectZero": underselect_zero,
        "underselectAny": underselect_any,
        "caused": caused,
        "underselectZeroRate": rate(underselect_zero, max(1, sealed)),
        "budgetHit": budget_hit,
        "budgetHitWithSurvivors": budget_hit_with_survivors,
        "stolenPresses": stolen_presses,
        "stolenTicksSum": stolen_sum,
        "survivorPresses": surv_presses,
        "meanStolenWhenSurvivors": (stolen_sum / surv_presses) if surv_presses > 0 else None,
        "d0Safe": d0_safe,
        "d0SafeWithUnderselect": d0_safe_with_underselect,
    }


# Primary KPIs: lower is better (except budgetNotReachedAliveShort which may rise
# as not-viable migrates — we treat the false-hopeless / underselect pair as primary).
# AP2: budgetHitWithSurvivors should fall once Screen→Verify steal lands.
# AP3: d0SafeWithUnderselect should fall once weak PassD0Safe is demoted.
PRIMARY_LOWER = (
    "hopelessWithAliveShort",
    "notViableAliveShort",
    "underselectZero",
    "caused",
    "budgetHitWithSurvivors",
    "d0SafeWithUnderselect",
)


def compare(baseline: dict[str, Any], candidate: dict[str, Any]) -> tuple[bool, list[str]]:
    lines: list[str] = []
    ok = True
    for key in PRIMARY_LOWER:
        b = int(baseline.get(key) or 0)
        c = int(candidate.get(key) or 0)
        delta = c - b
        status = "gain" if delta < 0 else ("flat" if delta == 0 else "regress")
        if delta > 0:
            ok = False
        lines.append(f"  {key}: {b} → {c} ({status}, Δ={delta:+d})")
    # Informational: budget-not-reached on alive-short should absorb old not-viable.
    b_b = int(baseline.get("budgetNotReachedAliveShort") or 0)
    c_b = int(candidate.get("budgetNotReachedAliveShort") or 0)
    lines.append(
        f"  budgetNotReachedAliveShort: {b_b} → {c_b} (info, expect rise if not-viable migrates)"
    )
    return ok, lines


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--baseline",
        type=Path,
        required=True,
        help="Baseline research pack dir or session.json (pre-AP1)",
    )
    ap.add_argument(
        "--candidate",
        type=Path,
        default=None,
        help="New research pack dir or session.json (post-AP1). Omit for baseline-only report.",
    )
    ap.add_argument("--json", action="store_true", help="Print machine JSON")
    args = ap.parse_args()

    base_data = load_session(args.baseline)
    base_score = score_session(base_data)

    if args.candidate is None:
        if args.json:
            print(json.dumps({"baseline": base_score}, indent=2))
        else:
            print("Fair-depth AP1 score (baseline only)")
            for k, v in base_score.items():
                print(f"  {k}: {v}")
            print(
                "\nRe-record Silent Hoveropolis StartPos Research Pack, then:\n"
                "  python tools/score_assist_pack_fair_depth.py "
                f"--baseline {args.baseline} --candidate <new_pack>"
            )
        return 0

    cand_data = load_session(args.candidate)
    cand_score = score_session(cand_data)
    improved, lines = compare(base_score, cand_score)

    if args.json:
        print(
            json.dumps(
                {"baseline": base_score, "candidate": cand_score, "improved": improved},
                indent=2,
            )
        )
    else:
        print("Fair-depth AP1 before/after")
        print("baseline:", args.baseline)
        print("candidate:", args.candidate)
        for line in lines:
            print(line)
        print("RESULT:", "PASS (improved or flat on primary KPIs)" if improved else "FAIL (regress)")

    return 0 if improved else 2


if __name__ == "__main__":
    sys.exit(main())
