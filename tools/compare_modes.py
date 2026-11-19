"""APR3: compare Live vs Hybrid scores; keep Frozen and Online in separate sections."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from real_assist.compare_support import compare
from real_assist.oracle import write_json
from real_assist.score import ScoreResult, score_session_file


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "sessions",
        nargs="+",
        type=Path,
        help="session JSON files and/or directories containing session_*.json",
    )
    parser.add_argument("--out", type=Path, default=None, help="Write compare JSON")
    args = parser.parse_args()

    paths: list[Path] = []
    seen: set[Path] = set()
    for entry in args.sessions:
        batch: list[Path] = []
        if entry.is_dir():
            for pattern in ("session_*.json", "*.session.json", "*_Frozen.json", "*_Online.json"):
                batch.extend(sorted(entry.glob(pattern)))
            for path in sorted(entry.glob("*.json")):
                if path.name.endswith(".score.json") or "invent" in path.name:
                    continue
                batch.append(path)
        elif entry.is_file():
            batch.append(entry)
        else:
            print(f"skip missing {entry}", file=sys.stderr)
        for path in batch:
            resolved = path.resolve()
            if resolved in seen:
                continue
            seen.add(resolved)
            paths.append(path)

    if not paths:
        print("no sessions found", file=sys.stderr)
        return 1

    scores: list[ScoreResult] = []
    for path in paths:
        try:
            scores.append(score_session_file(path))
        except Exception as exc:  # noqa: BLE001
            print(f"FAIL score {path}: {exc}", file=sys.stderr)
            return 1

    report = compare(scores)
    if args.out:
        write_json(args.out, report)
        print(f"wrote {args.out}")
    else:
        print(json.dumps(report, indent=2))

    for protocol, section in report["sections"].items():
        print(f"\n== {protocol} ==")
        for case in section["cases"]:
            live = case.get("Live") or {}
            hybrid = case.get("Hybrid") or {}
            print(
                f"{case['corpusId']}/{case['variant']}: "
                f"Live PHit={live.get('PhysicsHitRate')} Surv={live.get('SurviveRate')} | "
                f"Hybrid PHit={hybrid.get('PhysicsHitRate')} Surv={hybrid.get('SurviveRate')} "
                f"notes={case.get('notes')}"
            )

    return 2 if report["hardFailAny"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
