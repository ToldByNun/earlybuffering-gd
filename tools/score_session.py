"""APR3: score a Real Assist session JSON (PhysicsHitRate = acceptance)."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from real_assist.oracle import write_json
from real_assist.score import score_session_file


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path, help="session_*.json from saveDir/real_assist/")
    parser.add_argument("--physics", type=Path, default=None, help="PhysicsSafeSet JSON")
    parser.add_argument("--verifier", type=Path, default=None, help="VerifierSafeSet JSON")
    parser.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Write score JSON (default: <session>.score.json)",
    )
    parser.add_argument(
        "--require-physics",
        action="store_true",
        help="Exit 1 if PhysicsSafeSet missing (acceptance incomplete)",
    )
    args = parser.parse_args()
    if not args.session.is_file():
        print(f"missing session {args.session}", file=sys.stderr)
        return 1

    result = score_session_file(
        args.session, physics_path=args.physics, verifier_path=args.verifier
    )
    out = args.out or args.session.with_suffix(args.session.suffix + ".score.json")
    if out.suffix == ".json.json":
        out = Path(str(args.session) + ".score.json")
    write_json(out, result.to_dict())

    print(
        f"{result.corpusId}/{result.variant} {result.mode}/{result.protocol}\n"
        f"  SurviveRate={result.SurviveRate}\n"
        f"  PhysicsHitRate={result.PhysicsHitRate} (acceptance)\n"
        f"  VerifierHitRate={result.VerifierHitRate} (diagnostic only)\n"
        f"  InventRate={result.InventRate}\n"
        f"  DAF_exposed={result.DAF_exposed}\n"
        f"  planUs p50={result.planUs_p50} p95={result.planUs_p95}\n"
        f"  disagreements={result.verifierPhysicsDisagreements}\n"
        f"  diagnosticFailures={result.diagnosticFailures}\n"
        f"  hardFail={result.hardFail} {result.hardFailReasons}\n"
        f"  wrote {out}"
    )
    if result.hardFail:
        return 2
    if args.require_physics and not result.physicsSafeAvailable:
        print("PhysicsSafeSet required but missing", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
