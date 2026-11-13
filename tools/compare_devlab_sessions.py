#!/usr/bin/env python3
"""Compare EarlyBuffer Dev Lab session-*.json files (Gain / Flat / Regress).

Mirrors in-game AP11 EbDevSessionCompare:
  newest session vs median of the older N-1 sessions.

Usage:
  python tools/compare_devlab_sessions.py --dir <devtraces> --last 5
  python tools/compare_devlab_sessions.py --dir <devtraces> --last 5 --level-key <key>
  python tools/compare_devlab_sessions.py --dir <devtraces> --last 5 --any-level
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
import sys
from pathlib import Path


FLAT_EPS_ABS = 0.02
FLAT_EPS_REL = 0.05

# (id, higher_is_better)
METRICS = [
    ("liveEvalMatchRate", True),
    ("bestSurvShare", True),
    ("fidelityReproducedShare", True),
    ("deathAfterFireRate", False),
    ("medianTicksToDeath", False),
    ("hopelessRate", False),
    ("budgetHitRate", False),
    ("gatePassRate", True),
    ("meanChosenEndX", True),
    ("simNetHarmShare", False),
]


def rate(numer: int, denom: int) -> float | None:
    if denom <= 0:
        return None
    return numer / denom


def median(values: list[float]) -> float | None:
    if not values:
        return None
    return float(statistics.median(values))


def mean(values: list[float]) -> float | None:
    if not values:
        return None
    return float(statistics.mean(values))


def is_harm_net(net: str | None) -> bool:
    if not net:
        return False
    low = net.lower()
    return "harm" in low or "worse" in low


def metrics_from_session(data: dict, path: Path) -> dict:
    attempts = data.get("attempts") or []
    match_ok = match_den = 0
    best_surv = best_eval = 0
    fid_repr = fid_approx = fid_not = fid_den = 0
    daf_yes = daf_den = 0
    ticks_to_death: list[float] = []
    plan_down = gate_pass = hopeless = budget_hit = 0
    chosen_end_x: list[float] = []
    harm_net = net_den = 0
    evaluated = 0

    for att in attempts:
        ev = att.get("eval") or {}
        if ev.get("valid"):
            evaluated += 1
            fid_den += 1
            fid = ev.get("fidelity") or "notReproducible"
            if fid == "reproduced":
                fid_repr += 1
            elif fid == "playerOnlyApproximated":
                fid_approx += 1
            else:
                fid_not += 1

            claims = bool(ev.get("claimsAllowed"))
            has_surv = "bestSurvivingDelay" in ev
            has_eval = "bestEvaluatedDelay" in ev
            if claims and has_surv:
                best_surv += 1
            if claims and has_eval:
                best_eval += 1

            planned = ev.get("plannedDelay")
            if claims and has_surv and planned is not None and int(planned) >= 0:
                match_den += 1
                if int(planned) == int(ev.get("bestSurvivingDelay", -1)):
                    match_ok += 1

            net = ev.get("simNetClass") or ""
            if net:
                net_den += 1
                if is_harm_net(net):
                    harm_net += 1

            if planned is not None and int(planned) >= 0:
                for sample in ev.get("samples") or []:
                    if int(sample.get("delay", -1)) == int(planned):
                        if sample.get("endX") is not None:
                            chosen_end_x.append(float(sample["endX"]))
                        break

        for event in att.get("events") or []:
            kind = event.get("kind")
            down = bool(event.get("down", True))
            if kind == "planFinished" and down:
                plan_down += 1
                if event.get("budgetHit"):
                    budget_hit += 1
                reason = event.get("assistReason") or ""
                if reason.startswith("pass-hopeless"):
                    hopeless += 1
            if kind == "gatePass" and down:
                gate_pass += 1
            if kind == "outcome" and (
                "deathAfterFire" in event or "ticksToDeath" in event or "armVerifyOk" in event
            ):
                daf_den += 1
                if event.get("deathAfterFire"):
                    daf_yes += 1
                    ttd = event.get("ticksToDeath")
                    if ttd is not None and int(ttd) >= 0:
                        ticks_to_death.append(float(ttd))

    def put(key: str, value: float | None, out: dict) -> None:
        if value is not None:
            out[key] = value

    out = {
        "sessionId": data.get("sessionId") or "",
        "levelKey": data.get("levelKey") or "",
        "path": str(path),
        "attemptCount": len(attempts),
        "evaluatedAttempts": evaluated,
    }
    put("liveEvalMatchRate", rate(match_ok, match_den), out)
    put("bestSurvShare", rate(best_surv, evaluated), out)
    put("bestEvalShare", rate(best_eval, evaluated), out)
    put("deathAfterFireRate", rate(daf_yes, daf_den), out)
    put("medianTicksToDeath", median(ticks_to_death), out)
    put("fidelityReproducedShare", rate(fid_repr, fid_den), out)
    put("fidelityApproxShare", rate(fid_approx, fid_den), out)
    put("fidelityNotShare", rate(fid_not, fid_den), out)
    put("gatePassRate", rate(gate_pass, plan_down), out)
    put("hopelessRate", rate(hopeless, plan_down), out)
    put("budgetHitRate", rate(budget_hit, plan_down), out)
    put("meanChosenEndX", mean(chosen_end_x), out)
    put("simNetHarmShare", rate(harm_net, net_den), out)
    return out


def verdict_for(newest: float, baseline: float, higher_is_better: bool) -> str:
    scale = max(abs(baseline), 1.0)
    eps = max(FLAT_EPS_ABS, FLAT_EPS_REL * scale)
    delta = newest - baseline
    if abs(delta) <= eps:
        return "flat"
    improved = delta > 0 if higher_is_better else delta < 0
    return "gained" if improved else "regressed"


def list_sessions(traces_dir: Path) -> list[Path]:
    files = [
        p
        for p in traces_dir.iterdir()
        if p.is_file() and p.name.startswith("session-") and p.suffix == ".json"
    ]
    files.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return files


def baseline_median(older: list[dict], key: str) -> float | None:
    values = [float(s[key]) for s in older if key in s and s[key] is not None]
    return median(values)


def compare(sessions_newest_first: list[dict]) -> dict:
    newest = sessions_newest_first[0]
    older = sessions_newest_first[1:]
    deltas = []
    score = 0
    drivers: list[str] = []
    best_mag = -1.0
    driver = ""

    for mid, higher in METRICS:
        n = newest.get(mid)
        b = baseline_median(older, mid)
        entry = {
            "id": mid,
            "valid": n is not None and b is not None,
            "higherIsBetter": higher,
        }
        if entry["valid"]:
            entry["newest"] = float(n)
            entry["baseline"] = float(b)
            entry["delta"] = float(n) - float(b)
            entry["verdict"] = verdict_for(float(n), float(b), higher)
            if entry["verdict"] == "gained":
                score += 1
            elif entry["verdict"] == "regressed":
                score -= 1
            mag = abs(entry["delta"])
            if mag > best_mag and entry["verdict"] != "flat":
                best_mag = mag
                driver = mid
        else:
            entry["verdict"] = "flat"
        deltas.append(entry)

    if score > 0:
        verdict = "gained"
    elif score < 0:
        verdict = "regressed"
    else:
        verdict = "flat"

    if verdict != "flat":
        drivers = [d["id"] for d in deltas if d["valid"] and d["verdict"] == verdict]
        if drivers:
            driver = drivers[0]
    else:
        drivers = []
        driver = ""

    # oldest → newest for report
    sessions = list(reversed(sessions_newest_first))
    return {
        "schema": 1,
        "verdict": verdict,
        "driverMetric": driver or None,
        "drivers": drivers,
        "sessionCount": len(sessions),
        "sessions": sessions,
        "deltas": deltas,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dir", type=Path, required=True, help="devtraces directory")
    ap.add_argument("--last", type=int, default=5, help="last N sessions (2..20)")
    ap.add_argument("--level-key", default="", help="only sessions with this levelKey")
    ap.add_argument(
        "--any-level",
        action="store_true",
        help="do not filter by levelKey (default filters to newest levelKey)",
    )
    ap.add_argument("--out", type=Path, default=None, help="optional output JSON path")
    args = ap.parse_args()

    traces = args.dir
    if not traces.is_dir():
        print(f"error: not a directory: {traces}", file=sys.stderr)
        return 2

    last_n = max(2, min(20, int(args.last)))
    files = list_sessions(traces)
    if not files:
        print("error: no session-*.json found", file=sys.stderr)
        return 2

    level_filter = args.level_key
    same_level = not args.any_level
    selected: list[dict] = []
    for path in files:
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except Exception as exc:  # noqa: BLE001
            print(f"warn: skip {path.name}: {exc}", file=sys.stderr)
            continue
        metrics = metrics_from_session(data, path)
        if same_level and not level_filter:
            level_filter = metrics.get("levelKey") or ""
        if same_level and level_filter and metrics.get("levelKey") != level_filter:
            continue
        selected.append(metrics)
        if len(selected) >= last_n:
            break

    if len(selected) < 2:
        print(
            "error: need ≥2 sessions"
            + (" for this levelKey" if same_level else ""),
            file=sys.stderr,
        )
        return 2

    report = compare(selected)
    report["levelKey"] = level_filter
    report["lastN"] = last_n
    report["sameLevelOnly"] = same_level

    out_path = args.out
    if out_path is None:
        from datetime import datetime

        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        out_path = traces / f"compare_{stamp}.json"
    out_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    drivers = ",".join(report["drivers"][:3]) or (report.get("driverMetric") or "-")
    print(
        f"COMPARE {out_path.name} | verdict={report['verdict']} | "
        f"level={report['levelKey'] or '-'} | sessions={report['sessionCount']} | "
        f"drivers={drivers}"
    )
    print(f"wrote {out_path}")
    for d in report["deltas"]:
        if not d["valid"]:
            continue
        print(
            f"  {d['id']:28s} newest={d['newest']:.4f} baseline={d['baseline']:.4f} "
            f"delta={d['delta']:+.4f} → {d['verdict']}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
