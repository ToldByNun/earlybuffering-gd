"""APR1: build VerifierSafeSet request/result docs (diagnostic only).

Refuses unless reproduce.json status=pass. Offline this writes a pending-probe
document; in-game PlayLayer + shadow verifyDelay fills safeDelays / band.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from real_assist.oracle import build_oracle_document, load_json, require_reproduce_pass, write_json

FIXTURE_ROOT = ROOT / "fixtures" / "real_corpus"
DEFAULT_LIMIT = 16
SETTINGS = {
    "source": "verifyDelay",
    "requireSurvived": True,
    "requireTailComplete": True,
    "rejectUnknown": True,
    "rejectBudgetHit": True,
}


def build_one(corpus_id: str, sidecar_path: Path, limit: int) -> Path:
    corpus_dir = FIXTURE_ROOT / corpus_id
    require_reproduce_pass(corpus_dir)
    sidecar = load_json(sidecar_path)
    doc = build_oracle_document(
        kind="VerifierSafeSet",
        corpus_id=corpus_id,
        sidecar=sidecar,
        parent_sha=str(sidecar.get("parentSha256") or ""),
        limit=limit,
        settings_stamp=SETTINGS,
        status="pending-probe",
        reason="awaiting in-game shadow verifyDelay band at capture state",
    )
    out = sidecar_path.with_name(
        sidecar_path.name.replace(".sidecar.json", ".verifier_safe.json")
    )
    write_json(out, doc)
    sidecar["oracle"] = sidecar.get("oracle") or {}
    sidecar["oracle"]["verifierSafeSet"] = out.name
    write_json(sidecar_path, sidecar)
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--id", action="append", dest="ids")
    parser.add_argument("--family", default="pulse-shift-early-2", help="Variant family stem")
    parser.add_argument("--limit", type=int, default=DEFAULT_LIMIT)
    args = parser.parse_args()

    manifest_path = FIXTURE_ROOT / "manifest.json"
    if not manifest_path.is_file():
        print(f"missing {manifest_path}", file=sys.stderr)
        return 1
    manifest = load_json(manifest_path)
    ids = args.ids or list(manifest.get("ids") or [])
    fails = 0
    for corpus_id in ids:
        derived = FIXTURE_ROOT / "derived" / corpus_id
        matches = sorted(derived.glob(f"{args.family}*.sidecar.json"))
        if not matches:
            print(f"{corpus_id}: no sidecar for family={args.family}", file=sys.stderr)
            fails += 1
            continue
        for sidecar_path in matches:
            try:
                out = build_one(corpus_id, sidecar_path, args.limit)
                print(f"{corpus_id}: VerifierSafeSet pending -> {out.name}")
            except Exception as exc:  # noqa: BLE001 — CLI surface
                fails += 1
                print(f"{corpus_id}: REFUSED {exc}", file=sys.stderr)
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
