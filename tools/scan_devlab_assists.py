#!/usr/bin/env python3
"""Scan EarlyBuffer Dev Lab session JSON for assist failures."""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path


FOCUS_REASON_PREFIXES = ("pass-hopeless", "delayArm", "delay-", "fire", "gatePass", "pass-")


def reason_bucket(reason: str | None) -> str:
    if not reason:
        return "(none)"
    # Keep full reason for counting; caller may also group.
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


def scan_session(path: Path) -> None:
    data = json.loads(path.read_text(encoding="utf-8"))
    print("=" * 88)
    print(f"SESSION {data.get('sessionId')}  file={path.name}")
    print(
        f"  levelName={data.get('levelName')!r}  startPos={data.get('startPos')}  "
        f"practice={data.get('practice')}  attempts={len(data.get('attempts') or [])}  "
        f"recordings={data.get('recordingsDone')}/{data.get('recordingsTarget')}"
    )

    kind_counts: Counter[str] = Counter()
    reason_counts: Counter[str] = Counter()
    reason_on_kinds: Counter[tuple[str, str]] = Counter()

    plan_down_rows: list[dict] = []
    delay_fire_rows: list[dict] = []

    for att in data.get("attempts") or []:
        aid = att.get("attemptId")
        for ev in att.get("events") or []:
            kind = ev.get("kind") or "?"
            reason = ev.get("assistReason")
            kind_counts[kind] += 1
            reason_counts[reason_bucket(reason)] += 1
            if reason:
                reason_on_kinds[(kind, reason)] += 1

            down = bool(ev.get("down"))
            pl = player_fields(ev)
            clk = clock_fields(ev)

            if kind == "planFinished" and down:
                plan_down_rows.append(
                    {
                        "attemptId": aid,
                        "pressId": ev.get("pressId"),
                        "assistReason": reason,
                        "pressTick": ev.get("pressTick"),
                        "plannedDelay": ev.get("plannedDelay"),
                        **clk,
                        **pl,
                    }
                )

            if kind in ("delayArm", "fire") or (
                reason
                and (
                    reason.startswith("delay-")
                    or reason.startswith("fire@")
                )
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
        # Group by pressId for DOWN presses
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
            continue  # first DOWN was gatePass — skip
        # Prefer planFinished row for summary
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

    # Counts
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

    # Focus buckets
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
    # Also count gatePass kind
    focus["gatePass(kind)"] = kind_counts.get("gatePass", 0)
    focus["delayArm(kind)"] = kind_counts.get("delayArm", 0)
    focus["fire(kind)"] = kind_counts.get("fire", 0)

    print("\n  focus buckets:")
    for k, n in sorted(focus.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"    {n:4d}  {k}")

    print("\n  planFinished DOWN presses:")
    print(
        f"    {'att':>3} {'press':>5} {'tick':>5} {'delay':>5} "
        f"{'simTps':>6} {'suspect':>7} {'simDt':>6} "
        f"{'jumped':>6} {'ground':>6} {'x':>10} {'y':>8}  reason"
    )
    for row in plan_down_rows:
        print(
            f"    {row['attemptId']:>3} {fmt_num(row['pressId'],0):>5} "
            f"{fmt_num(row['pressTick'],0):>5} {fmt_num(row['plannedDelay'],0):>5} "
            f"{fmt_num(row['simTps'],1):>6} {fmt_num(row['speedScaleSuspect']):>7} "
            f"{fmt_num(row['simDt'],3):>6} "
            f"{fmt_num(row['hasEverJumped']):>6} {fmt_num(row['isOnGround']):>6} "
            f"{fmt_num(row['x'],1):>10} {fmt_num(row['y'],1):>8}  "
            f"{row['assistReason']}"
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
    successes = [
        r
        for r in delay_fire_rows
        if r["kind"] in ("delayArm", "fire")
        or (r.get("assistReason") or "").startswith("fire@")
        or (r.get("assistReason") or "").startswith("delay-")
    ]
    # Deduplicate to delayArm/fire kinds primarily
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

    # Quick clock anomaly summary on planFinished downs
    if plan_down_rows:
        tps = Counter(r.get("simTps") for r in plan_down_rows)
        sus = Counter(r.get("speedScaleSuspect") for r in plan_down_rows)
        dts = Counter(r.get("simDt") for r in plan_down_rows)
        print("\n  clock on planFinished DOWN:")
        print(f"    simTps={dict(tps)}  speedScaleSuspect={dict(sus)}  simDt={dict(dts)}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "paths",
        nargs="*",
        type=Path,
        default=[
            Path(r"C:\Users\mikaj\AppData\Local\GeometryDash\geode\mods\audesign.earlybuffer\devtraces")
            / name
            for name in (
                "session-1793312888777.json",
                "session-1793312934335.json",
                "session-1793313116877.json",
            )
        ],
    )
    args = ap.parse_args()
    for p in args.paths:
        if not p.exists():
            print(f"MISSING {p}", file=sys.stderr)
            continue
        scan_session(p)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
