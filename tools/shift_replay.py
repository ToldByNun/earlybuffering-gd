"""APR1: synthesize on-time / pulse-shift / edge-only / drop-click variants into derived/."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from real_assist.gdr2 import export_gdr2, parse_gdr2
from real_assist.shift import Family, apply_shift, default_click_index, pair_clicks

FIXTURE_ROOT = ROOT / "fixtures" / "real_corpus"
DEFAULT_KS = list(range(1, 9))
SHIFT_FAMILIES: list[Family] = [
    "edge-only-early",
    "pulse-shift-early",
    "edge-only-late",
    "pulse-shift-late",
]


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def write_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def synthesize_id(
    corpus_id: str,
    click_index: int | None,
    ks: list[int],
    families: list[Family],
    include_drop: bool,
) -> int:
    folder = FIXTURE_ROOT / corpus_id
    clear = folder / "clear.gdr2"
    if not clear.is_file():
        raise FileNotFoundError(f"missing {clear}; run tools/sync_real_corpus.py")
    replay = parse_gdr2(clear)
    pairs = pair_clicks(replay.inputs)
    if not pairs:
        raise ValueError(f"{corpus_id}: no paired clicks")
    chosen = default_click_index(pairs) if click_index is None else click_index
    parent_sha = sha256_file(clear)
    out_dir = FIXTURE_ROOT / "derived" / corpus_id
    out_dir.mkdir(parents=True, exist_ok=True)

    jobs: list[tuple[Family, int]] = [("on-time", 0)]
    for family in families:
        for k in ks:
            jobs.append((family, k))
    if include_drop:
        jobs.append(("drop-click", 0))

    written = 0
    index_rows = []
    for family, k in jobs:
        result = apply_shift(
            replay, family, k=k, click_index=None if family == "on-time" else chosen
        )
        stem = result.family if family == "on-time" else f"{result.family}__c{chosen}"
        gdr_path = out_dir / f"{stem}.gdr2"
        side_path = out_dir / f"{stem}.sidecar.json"
        export_gdr2(result.replay, gdr_path)
        sidecar = {
            **result.sidecar,
            "corpusId": corpus_id,
            "parentClear": "clear.gdr2",
            "parentSha256": parent_sha,
            "derivedFile": gdr_path.name,
            "tps": replay.framerate,
            "eventCount": len(result.replay.inputs),
            "synthesizedAt": utc_now(),
            "oracle": {
                "verifierSafeSet": None,
                "physicsSafeSet": None,
                "requiresReproducePass": True,
            },
        }
        write_json(side_path, sidecar)
        index_rows.append(
            {
                "family": result.family,
                "file": gdr_path.name,
                "sidecar": side_path.name,
                "clickIndex": result.click_index,
                "clamped": result.clamped,
                "originalHoldLength": sidecar.get("originalHoldLength"),
                "newHoldLength": sidecar.get("newHoldLength"),
            }
        )
        written += 1
        print(
            f"{corpus_id}: {stem} events={len(result.replay.inputs)} "
            f"clamped={result.clamped} hold={sidecar.get('originalHoldLength')}->"
            f"{sidecar.get('newHoldLength')}"
        )

    write_json(
        out_dir / "index.json",
        {
            "schema": 1,
            "corpusId": corpus_id,
            "parentSha256": parent_sha,
            "clickIndex": chosen,
            "pairCount": len(pairs),
            "variants": index_rows,
            "updatedAt": utc_now(),
        },
    )
    return written


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--id",
        action="append",
        dest="ids",
        help="Corpus id (repeatable). Default: all manifest ids.",
    )
    parser.add_argument(
        "--click",
        type=int,
        default=None,
        help="Paired-click index to edit (default: middle pair)",
    )
    parser.add_argument(
        "--k",
        type=int,
        nargs="*",
        default=None,
        help="Shift amounts (default: 1..8)",
    )
    parser.add_argument(
        "--no-drop",
        action="store_true",
        help="Skip drop-click variant",
    )
    args = parser.parse_args()
    manifest_path = FIXTURE_ROOT / "manifest.json"
    if not manifest_path.is_file():
        print(f"missing {manifest_path}; run sync first", file=sys.stderr)
        return 1
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    ids = args.ids or list(manifest.get("ids") or [])
    ks = args.k if args.k is not None else DEFAULT_KS
    total = 0
    for corpus_id in ids:
        total += synthesize_id(
            corpus_id,
            click_index=args.click,
            ks=ks,
            families=SHIFT_FAMILIES,
            include_drop=not args.no_drop,
        )
    print(f"wrote {total} derived variants under {FIXTURE_ROOT / 'derived'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
