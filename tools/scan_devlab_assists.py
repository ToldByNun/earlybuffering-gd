#!/usr/bin/env python3
"""Scan EarlyBuffer Dev Lab session JSON / Research Packs for assist mismatches.

Accepts:
  - session-*.json files
  - research/research_<stamp>/ folders (uses session.json + optional report.json)
  - a directory (scans session-*.json inside; research packs if MANIFEST.json present)

Examples:
  python tools/scan_devlab_assists.py path/to/session-….json
  python tools/scan_devlab_assists.py path/to/research/research_20260101_120000
  python tools/scan_devlab_assists.py %LocalAppData%/GeometryDash/geode/mods/audesign.earlybuffer/devtraces

For Gain/Flat/Regress across many sessions, use:
  python tools/compare_devlab_sessions.py --dir <devtraces> --last 5
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path


def reason_bucket(reason: str | None) -> str:
    if not reason:
        return "(none)"
    return reason


def player_fields(ev: dict) -> dict:
    wit = ev.get("witness") or {}
    pl = wit.get("player") or {}
    return {
        "hasEverJumped": pl.get("hasEverJumped"),
        "isOnGround": pl.get("isOnGround"),
        "x": pl.get("x"),
        "y": pl.get("y"),
    }


def clock_fields(ev: dict) -> dict:
    clk = ev.get("clock") or {}
    if not clk and isinstance(ev.get("witness"), dict):
        clk = (ev.get("witness") or {}).get("clock") or {}
    return {
        "simTps": clk.get("simTps"),
        "speedScaleSuspect": clk.get("speedScaleSuspect"),
        "simDt": clk.get("simDt"),
        "attemptTick": clk.get("attemptTick"),
        "timeWarp": clk.get("timeWarp"),
    }


def fmt_num(v, digits=2):
    if v is None:
        return "-"
    if isinstance(v, bool):
        return str(v).lower()
    if isinstance(v, float):
        return f"{v:.{digits}f}"
    return str(v)


def resolve_session_paths(path: Path) -> list[tuple[Path, Path | None]]:
    """Return list of (session_json, pack_dir_or_None)."""
    if not path.exists():
        return []

    if path.is_file():
        if path.suffix.lower() == ".json":
            # session file or accidental report — prefer if it looks like a session
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
            except Exception:
                return [(path, None)]
            if "attempts" in data or path.name.startswith("session-"):
                return [(path, None)]
            if path.name == "session.json":
                return [(path, path.parent if (path.parent / "MANIFEST.json").exists() else None)]
        return [(path, None)]

    # Directory: research pack?
    manifest = path / "MANIFEST.json"
    session = path / "session.json"
    if manifest.exists() or session.exists():
        if session.exists():
            return [(session, path)]
        print(f"WARN pack {path} has no session.json", file=sys.stderr)
        return []

    # Directory of sessions / mixed
    out: list[tuple[Path, Path | None]] = []
    for child in sorted(path.iterdir()):
        if child.is_file() and child.name.startswith("session-") and child.suffix == ".json":
            out.append((child, None))
        elif child.is_dir() and (
            (child / "MANIFEST.json").exists() or (child / "session.json").exists()
        ):
            if (child / "session.json").exists():
                out.append((child / "session.json", child))
    return out


def scan_eval_mismatches(data: dict) -> list[dict]:
    rows: list[dict] = []
    for att in data.get("attempts") or []:
        ev = att.get("eval") or {}
        if not ev.get("valid"):
            continue
        planned = ev.get("plannedDelay")
        best_surv = ev.get("bestSurvivingDelay") if ev.get("claimsAllowed") else None
        best_eval = ev.get("bestEvaluatedDelay") if ev.get("claimsAllowed") else None
        mismatch = False
        note = []
        if ev.get("liveVsEvalMismatch"):
            mismatch = True
            note.append("liveVsEvalMismatch")
        if (
            planned is not None
            and best_surv is not None
            and int(planned) >= 0
            and int(planned) != int(best_surv)
        ):
            mismatch = True
            note.append(f"planned={planned}≠bestSurv={best_surv}")
        if planned is not None and int(planned) > 0 and best_surv is None and ev.get("claimsAllowed"):
            mismatch = True
            note.append("bestSurv=none")
        if not mismatch and not note:
            # Still record evaluated attempts when explicitly asked via summary counts
            pass
        if mismatch or note:
            rows.append(
                {
                    "attemptId": att.get("attemptId"),
                    "fidelity": ev.get("fidelity"),
                    "claimsAllowed": ev.get("claimsAllowed"),
                    "approx": ev.get("approx"),
                    "plannedDelay": planned,
                    "bestSurvivingDelay": best_surv,
                    "bestEvaluatedDelay": best_eval,
                    "simNetClass": ev.get("simNetClass"),
                    "objectiveId": ev.get("objectiveId"),
                    "note": ";".join(note) if note else "-",
                    "sampleCount": len(ev.get("samples") or []),
                }
            )
    return rows


def scan_eval_summary(data: dict) -> dict:
    fid = Counter()
    match = miss = none_surv = evaluated = 0
    for att in data.get("attempts") or []:
        ev = att.get("eval") or {}
        if not ev.get("valid"):
            continue
        evaluated += 1
        fid[ev.get("fidelity") or "notReproducible"] += 1
        planned = ev.get("plannedDelay")
        best_surv = ev.get("bestSurvivingDelay") if ev.get("claimsAllowed") else None
        if planned is not None and int(planned) >= 0 and ev.get("claimsAllowed"):
            if best_surv is None:
                none_surv += 1
            elif int(planned) == int(best_surv):
                match += 1
            else:
                miss += 1
    return {
        "evaluated": evaluated,
        "fidelity": dict(fid),
        "liveEvalMatch": match,
        "liveEvalMismatch": miss,
        "bestSurvNone": none_surv,
    }


def scan_mode_timeline(data: dict) -> list[dict]:
    rows: list[dict] = []
    for att in data.get("attempts") or []:
        aid = att.get("attemptId")
        for ev in att.get("events") or []:
            kind = ev.get("kind")
            if kind not in ("modeTransition", "checkpoint"):
                continue
            rows.append(
                {
                    "attemptId": aid,
                    "kind": kind,
                    "timelineChange": ev.get("timelineChange"),
                    "effectTick": ev.get("effectTick"),
                    "gameModeKind": ev.get("gameModeKind"),
                    "fromGameModeKind": ev.get("fromGameModeKind"),
                    "playerSpeed": ev.get("playerSpeed"),
                    "upsideDown": ev.get("upsideDown"),
                    "dual": ev.get("dual"),
                    "playerX": ev.get("playerX"),
                }
            )
    return rows


def print_pack_banner(pack_dir: Path) -> None:
    print(f"  researchPack={pack_dir}")
    manifest = pack_dir / "MANIFEST.json"
    if manifest.exists():
        try:
            man = json.loads(manifest.read_text(encoding="utf-8"))
            print(
                f"  manifest schema={man.get('schema')} sessionId={man.get('sessionId')!r} "
                f"levelKey={man.get('levelKey')!r} files={len(man.get('files') or [])}"
            )
        except Exception as exc:  # noqa: BLE001
            print(f"  manifest read failed: {exc}")
    report = pack_dir / "report.json"
    if report.exists():
        try:
            rep = json.loads(report.read_text(encoding="utf-8"))
            overlay = rep.get("overlay") or rep.get("overlayLine") or ""
            if overlay:
                print(f"  report.overlay={overlay}")
        except Exception:
            pass
    txt = pack_dir / "report.txt"
    if txt.exists() and not report.exists():
        line = txt.read_text(encoding="utf-8").strip().splitlines()
        if line:
            print(f"  report.txt={line[0]}")


def scan_session(path: Path, pack_dir: Path | None = None) -> None:
    data = json.loads(path.read_text(encoding="utf-8"))
    print("=" * 88)
    print(f"SESSION {data.get('sessionId')}  file={path.name}")
    if pack_dir is not None:
        print_pack_banner(pack_dir)
    print(
        f"  levelName={data.get('levelName')!r}  levelKey={data.get('levelKey')!r}  "
        f"startPos={data.get('startPos')}  practice={data.get('practice')}  "
        f"attempts={len(data.get('attempts') or [])}  "
        f"recordings={data.get('recordingsDone')}/{data.get('recordingsTarget')}"
    )

    kind_counts: Counter[str] = Counter()
    reason_counts: Counter[str] = Counter()

    plan_down_rows: list[dict] = []
    delay_fire_rows: list[dict] = []
    planner_paths: Counter[str] = Counter()
    survival_classes: Counter[str] = Counter()

    for att in data.get("attempts") or []:
        aid = att.get("attemptId")
        for ev in att.get("events") or []:
            kind = ev.get("kind") or "?"
            reason = ev.get("assistReason")
            kind_counts[kind] += 1
            reason_counts[reason_bucket(reason)] += 1

            pl = player_fields(ev)
            clk = clock_fields(ev)
            down = bool(ev.get("down"))

            if kind == "planFinished":
                pp = ev.get("plannerPath")
                if pp:
                    planner_paths[str(pp)] += 1
                for note in ev.get("candNotes") or []:
                    sc = note.get("survivalClass")
                    if sc:
                        survival_classes[str(sc)] += 1

            if kind == "planFinished" and down:
                plan_down_rows.append(
                    {
                        "attemptId": aid,
                        "pressId": ev.get("pressId"),
                        "assistReason": reason,
                        "pressTick": ev.get("pressTick"),
                        "plannedDelay": ev.get("plannedDelay"),
                        "plannerPath": ev.get("plannerPath"),
                        "gameModeKind": ev.get("gameModeKind"),
                        **clk,
                        **pl,
                    }
                )

            if kind in ("delayArm", "fire") or (
                reason and (reason.startswith("delay-") or reason.startswith("fire@"))
            ):
                delay_fire_rows.append(
                    {
                        "attemptId": aid,
                        "kind": kind,
                        "down": down,
                        "assistReason": reason,
                        "pressTick": ev.get("pressTick"),
                        "plannedDelay": ev.get("plannedDelay"),
                        "plannedFireTick": ev.get("plannedFireTick"),
                        "actualFireTick": ev.get("actualFireTick"),
                        **clk,
                        **pl,
                    }
                )

    # First DOWN press per attempt that is NOT gatePass
    first_bad_down: list[dict] = []
    for att in data.get("attempts") or []:
        aid = att.get("attemptId")
        down_press_ids: list[int] = []
        by_press: dict[int, list[dict]] = {}
        for ev in att.get("events") or []:
            if not ev.get("down"):
                continue
            pid = ev.get("pressId")
            if pid is None:
                continue
            if pid not in by_press:
                by_press[pid] = []
                down_press_ids.append(pid)
            by_press[pid].append(ev)
        if not down_press_ids:
            continue
        first_pid = down_press_ids[0]
        evs = by_press[first_pid]
        kinds = {e.get("kind") for e in evs}
        if "gatePass" in kinds:
            continue
        plan = next((e for e in evs if e.get("kind") == "planFinished"), None)
        src = plan or evs[0]
        pl = player_fields(src)
        clk = clock_fields(src)
        first_bad_down.append(
            {
                "attemptId": aid,
                "pressId": first_pid,
                "kinds": ",".join(sorted(k for k in kinds if k)),
                "assistReason": src.get("assistReason"),
                "pressTick": src.get("pressTick"),
                "plannedDelay": src.get("plannedDelay"),
                **clk,
                **pl,
            }
        )

    print("\n  kind counts:")
    for k, n in kind_counts.most_common():
        print(f"    {k:16s} {n}")

    print("\n  assistReason counts (all events with reason):")
    for r, n in reason_counts.most_common():
        if r == "(none)":
            continue
        print(f"    {n:4d}  {r}")
    none_n = reason_counts.get("(none)", 0)
    if none_n:
        print(f"    {none_n:4d}  (none)")

    focus = Counter()
    for r, n in reason_counts.items():
        if r == "(none)":
            continue
        base = r.split("@", 1)[0]
        if base.startswith("fire"):
            focus["fire"] += n
        elif base.startswith("delay"):
            focus["delayArm/delay-*"] += n
        elif base.startswith("pass-hopeless"):
            focus["pass-hopeless"] += n
        elif base.startswith("gatePass") or r == "gatePass":
            focus["gatePass"] += n
        elif base.startswith("pass-"):
            focus[f"pass-other:{base}"] += n
        else:
            focus[f"other:{base}"] += n
    focus["gatePass(kind)"] = kind_counts.get("gatePass", 0)
    focus["delayArm(kind)"] = kind_counts.get("delayArm", 0)
    focus["fire(kind)"] = kind_counts.get("fire", 0)
    focus["modeTransition(kind)"] = kind_counts.get("modeTransition", 0)

    print("\n  focus buckets:")
    for k, n in sorted(focus.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"    {n:4d}  {k}")

    if planner_paths:
        print("\n  plannerPath counts:")
        for k, n in planner_paths.most_common():
            print(f"    {n:4d}  {k}")

    if survival_classes:
        print("\n  candNotes survivalClass counts:")
        for k, n in survival_classes.most_common():
            print(f"    {n:4d}  {k}")

    # Sealed eval summary + mismatches (AP3/AP10)
    eval_sum = scan_eval_summary(data)
    print("\n  sealed eval:")
    print(
        f"    evaluated={eval_sum['evaluated']}  fidelity={eval_sum['fidelity']}  "
        f"match={eval_sum['liveEvalMatch']}  mismatch={eval_sum['liveEvalMismatch']}  "
        f"bestSurvNone={eval_sum['bestSurvNone']}"
    )
    mismatches = scan_eval_mismatches(data)
    if mismatches:
        print("\n  live↔eval mismatches (sealed eval):")
        print(
            f"    {'att':>3} {'fid':>22} {'plan':>5} {'surv':>5} {'eval':>5}  note"
        )
        for row in mismatches:
            print(
                f"    {fmt_num(row['attemptId'],0):>3} {fmt_num(row['fidelity']):>22} "
                f"{fmt_num(row['plannedDelay'],0):>5} {fmt_num(row['bestSurvivingDelay'],0):>5} "
                f"{fmt_num(row['bestEvaluatedDelay'],0):>5}  {row['note']}"
            )
    else:
        print("\n  live↔eval mismatches: (none)")

    timeline = scan_mode_timeline(data)
    print(f"\n  modeTransition / checkpoint events: {len(timeline)}")
    if timeline:
        print(
            f"    {'att':>3} {'kind':>14} {'change':>10} {'tick':>5} "
            f"{'mode':>8} {'from':>8} {'spd':>5} {'ud':>3} {'dual':>4}  x"
        )
        for row in timeline[:40]:
            print(
                f"    {fmt_num(row['attemptId'],0):>3} {fmt_num(row['kind']):>14} "
                f"{fmt_num(row['timelineChange']):>10} {fmt_num(row['effectTick'],0):>5} "
                f"{fmt_num(row['gameModeKind']):>8} {fmt_num(row['fromGameModeKind']):>8} "
                f"{fmt_num(row['playerSpeed'],2):>5} {fmt_num(row['upsideDown']):>3} "
                f"{fmt_num(row['dual']):>4}  {fmt_num(row['playerX'],1)}"
            )
        if len(timeline) > 40:
            print(f"    … {len(timeline) - 40} more")

    print("\n  planFinished DOWN presses:")
    print(
        f"    {'att':>3} {'press':>5} {'tick':>5} {'delay':>5} "
        f"{'simTps':>6} {'suspect':>7} {'simDt':>6} "
        f"{'jumped':>6} {'ground':>6} {'x':>10} {'y':>8}  path  reason"
    )
    for row in plan_down_rows:
        print(
            f"    {row['attemptId']:>3} {fmt_num(row['pressId'],0):>5} "
            f"{fmt_num(row['pressTick'],0):>5} {fmt_num(row['plannedDelay'],0):>5} "
            f"{fmt_num(row['simTps'],1):>6} {fmt_num(row['speedScaleSuspect']):>7} "
            f"{fmt_num(row['simDt'],3):>6} "
            f"{fmt_num(row['hasEverJumped']):>6} {fmt_num(row['isOnGround']):>6} "
            f"{fmt_num(row['x'],1):>10} {fmt_num(row['y'],1):>8}  "
            f"{fmt_num(row.get('plannerPath'))}  {row['assistReason']}"
        )

    print("\n  first DOWN press per attempt that is NOT gatePass:")
    if not first_bad_down:
        print("    (none — every first DOWN was gatePass)")
    else:
        print(
            f"    {'att':>3} {'press':>5} {'tick':>5} {'delay':>5} "
            f"{'simTps':>6} {'suspect':>7} {'simDt':>6} "
            f"{'jumped':>6} {'ground':>6} {'x':>10} {'y':>8}  reason  kinds"
        )
        for row in first_bad_down:
            print(
                f"    {row['attemptId']:>3} {fmt_num(row['pressId'],0):>5} "
                f"{fmt_num(row['pressTick'],0):>5} {fmt_num(row['plannedDelay'],0):>5} "
                f"{fmt_num(row['simTps'],1):>6} {fmt_num(row['speedScaleSuspect']):>7} "
                f"{fmt_num(row['simDt'],3):>6} "
                f"{fmt_num(row['hasEverJumped']):>6} {fmt_num(row['isOnGround']):>6} "
                f"{fmt_num(row['x'],1):>10} {fmt_num(row['y'],1):>8}  "
                f"{row['assistReason']}  [{row['kinds']}]"
            )

    print("\n  delayArm / fire successes:")
    shown = [r for r in delay_fire_rows if r["kind"] in ("delayArm", "fire")]
    if not shown:
        print("    (none)")
    else:
        print(
            f"    {'att':>3} {'kind':>8} {'down':>4} {'tick':>5} {'delay':>5} "
            f"{'pFire':>6} {'aFire':>6} {'simTps':>6} {'suspect':>7}  reason"
        )
        for row in shown:
            print(
                f"    {row['attemptId']:>3} {row['kind']:>8} "
                f"{str(row['down']).lower():>4} {fmt_num(row['pressTick'],0):>5} "
                f"{fmt_num(row['plannedDelay'],0):>5} "
                f"{fmt_num(row['plannedFireTick'],0):>6} "
                f"{fmt_num(row['actualFireTick'],0):>6} "
                f"{fmt_num(row['simTps'],1):>6} {fmt_num(row['speedScaleSuspect']):>7}  "
                f"{row['assistReason']}"
            )

    if plan_down_rows:
        tps = Counter(r.get("simTps") for r in plan_down_rows)
        sus = Counter(r.get("speedScaleSuspect") for r in plan_down_rows)
        dts = Counter(r.get("simDt") for r in plan_down_rows)
        print("\n  clock on planFinished DOWN:")
        print(f"    simTps={dict(tps)}  speedScaleSuspect={dict(sus)}  simDt={dict(dts)}")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Scan Dev Lab session JSON or Research Pack folders for assist mismatches.",
        epilog="Compare Gain/Flat/Regress: python tools/compare_devlab_sessions.py --dir <devtraces> --last 5",
    )
    ap.add_argument(
        "paths",
        nargs="+",
        type=Path,
        help="session-*.json, research pack folder, or directory containing them",
    )
    args = ap.parse_args()

    any_ok = False
    for raw in args.paths:
        resolved = resolve_session_paths(raw)
        if not resolved:
            print(f"MISSING/EMPTY {raw}", file=sys.stderr)
            continue
        for session_path, pack_dir in resolved:
            if not session_path.exists():
                print(f"MISSING {session_path}", file=sys.stderr)
                continue
            try:
                scan_session(session_path, pack_dir)
                any_ok = True
            except Exception as exc:  # noqa: BLE001
                print(f"ERROR {session_path}: {exc}", file=sys.stderr)
    return 0 if any_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
