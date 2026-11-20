"""APR1: synthesize on-time / pulse-shift / edge-only / drop-click variants into derived/."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from real_assist.dense import (
    DEFAULT_SEQUENCE_LENGTHS,
    parse_click_spec,
    parse_sequence_lengths,
    sequence_starts,
)
from real_assist.gdr2 import export_gdr2, parse_gdr2
from real_assist.shift import Family, apply_multi_shift, default_click_index, pair_clicks

FIXTURE_ROOT = ROOT / "fixtures" / "real_corpus"
DEFAULT_KS = list(range(1, 9))
# Multi-click sequences stay lighter: representative earliness only.
SEQUENCE_KS = [2, 4, 6]
SEQUENCE_FAMILIES: list[Family] = [
    "edge-only-early",
    "pulse-shift-early",
]
SHIFT_FAMILIES: list[Family] = [
    "edge-only-early",
    "pulse-shift-early",
    "edge-only-late",
    "pulse-shift-late",
]
# Dense lab: 8 click quantiles × single + seq2/3 × windows later in oracle freeze.
DEFAULT_DENSE_CLICKS = "quantile:8"
DEFAULT_DENSE_SEQUENCES = "1,2,3"


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def write_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def default_save_corpus() -> Path:
    local = os.environ.get("LOCALAPPDATA")
    if not local:
        raise SystemExit("LOCALAPPDATA unset; pass --install-save PATH")
    return Path(local) / "GeometryDash" / "geode" / "mods" / "audesign.earlybuffer" / "real_corpus"


def synthesize_id(
    corpus_id: str,
    click_index: int | None,
    ks: list[int],
    families: list[Family],
    include_drop: bool,
    install_root: Path | None = None,
    click_spec: str | None = None,
    quiet: bool = False,
    sequence_lengths: list[int] | None = None,
) -> int:
    folder = FIXTURE_ROOT / corpus_id
    clear = folder / "clear.gdr2"
    if not clear.is_file():
        raise FileNotFoundError(f"missing {clear}; run tools/sync_real_corpus.py")
    replay = parse_gdr2(clear)
    pairs = pair_clicks(replay.inputs)
    if not pairs:
        raise ValueError(f"{corpus_id}: no paired clicks")
    if click_spec:
        click_indices = parse_click_spec(click_spec, len(pairs))
    elif click_index is not None:
        click_indices = [click_index]
    else:
        click_indices = [default_click_index(pairs)]
    if not click_indices:
        raise ValueError(f"{corpus_id}: empty click sample")
    lengths = sequence_lengths or [1]
    parent_sha = sha256_file(clear)
    out_dir = FIXTURE_ROOT / "derived" / corpus_id
    out_dir.mkdir(parents=True, exist_ok=True)

    jobs: list[tuple[Family, int, int, list[int]]] = []
    # on-time once
    jobs.append(("on-time", 0, 0, []))
    for length in lengths:
        starts = sequence_starts(click_indices, len(pairs), length)
        if length <= 1:
            fams = families
            use_ks = ks
            for start in starts:
                for family in fams:
                    for k in use_ks:
                        jobs.append((family, k, length, [start]))
                if include_drop:
                    jobs.append(("drop-click", 0, length, [start]))
        else:
            # Multi-click: early families + representative k only.
            for start in starts:
                seq = list(range(start, start + length))
                for family in SEQUENCE_FAMILIES:
                    for k in SEQUENCE_KS:
                        jobs.append((family, k, length, seq))

    written = 0
    index_rows = []
    seen_stems: set[str] = set()
    for family, k, length, seq in jobs:
        result = apply_multi_shift(replay, family, k=k, click_indices=seq)
        if family == "on-time":
            stem = result.family
        elif length <= 1:
            stem = f"{result.family}__c{seq[0]}"
        else:
            stem = f"{result.family}__c{seq[0]}s{length}"
        if stem in seen_stems:
            continue
        seen_stems.add(stem)
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
        if install_root is not None:
            install_dir = install_root / "derived" / corpus_id
            install_dir.mkdir(parents=True, exist_ok=True)
            shutil.copy2(gdr_path, install_dir / gdr_path.name)
            shutil.copy2(side_path, install_dir / side_path.name)
        index_rows.append(
            {
                "family": result.family,
                "file": gdr_path.name,
                "sidecar": side_path.name,
                "clickIndex": result.click_index,
                "clickIndices": sidecar.get("clickIndices") or seq,
                "sequenceLength": int(sidecar.get("sequenceLength") or length or 1),
                "clamped": result.clamped,
                "originalHoldLength": sidecar.get("originalHoldLength"),
                "newHoldLength": sidecar.get("newHoldLength"),
            }
        )
        written += 1
        if not quiet:
            print(
                f"{corpus_id}: {stem} seq={length} events={len(result.replay.inputs)} "
                f"clamped={result.clamped}"
            )

    index_payload = {
        "schema": 1,
        "corpusId": corpus_id,
        "parentSha256": parent_sha,
        "clickIndices": click_indices,
        "clickSpec": click_spec or ("list:" + ",".join(str(c) for c in click_indices)),
        "sequenceLengths": lengths,
        "pairCount": len(pairs),
        "variants": index_rows,
        "updatedAt": utc_now(),
    }
    write_json(out_dir / "index.json", index_payload)
    if install_root is not None:
        write_json(install_root / "derived" / corpus_id / "index.json", index_payload)
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
        help="Single paired-click index (default: middle pair). Ignored if --clicks/--dense.",
    )
    parser.add_argument(
        "--clicks",
        type=str,
        default=None,
        help="Click sample: middle|all|quantile:N|every:N|list:1,2,3",
    )
    parser.add_argument(
        "--dense",
        action="store_true",
        help=(
            f"Lab dense sample ({DEFAULT_DENSE_CLICKS}, sequences {DEFAULT_DENSE_SEQUENCES}, "
            "families x k) for multi-click + multi-window probes"
        ),
    )
    parser.add_argument(
        "--sequences",
        type=str,
        default=None,
        help="Comma sequence lengths (default: 1; dense default 1,2,3)",
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
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Only print summary counts",
    )
    parser.add_argument(
        "--install-save",
        nargs="?",
        const="",
        default=None,
        help="Also copy derived/ into mod save real_corpus/ (for in-game runner)",
    )
    args = parser.parse_args()
    manifest_path = FIXTURE_ROOT / "manifest.json"
    if not manifest_path.is_file():
        print(f"missing {manifest_path}; run sync first", file=sys.stderr)
        return 1
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    ids = args.ids or list(manifest.get("ids") or [])
    ks = args.k if args.k is not None else DEFAULT_KS
    click_spec = args.clicks
    seq_spec = args.sequences
    if args.dense:
        click_spec = click_spec or DEFAULT_DENSE_CLICKS
        seq_spec = seq_spec or DEFAULT_DENSE_SEQUENCES
    sequence_lengths = parse_sequence_lengths(seq_spec) if seq_spec else [1]
    install_root = None
    if args.install_save is not None:
        install_root = Path(args.install_save) if args.install_save else default_save_corpus()
        install_root.mkdir(parents=True, exist_ok=True)
    total = 0
    for corpus_id in ids:
        total += synthesize_id(
            corpus_id,
            click_index=None if click_spec else args.click,
            ks=ks,
            families=SHIFT_FAMILIES,
            include_drop=not args.no_drop,
            install_root=install_root,
            click_spec=click_spec,
            quiet=args.quiet or args.dense,
            sequence_lengths=sequence_lengths,
        )
    print(f"wrote {total} derived variants under {FIXTURE_ROOT / 'derived'}")
    if install_root is not None:
        print(f"installed derived -> {install_root / 'derived'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
