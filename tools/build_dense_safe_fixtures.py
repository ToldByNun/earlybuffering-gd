"""Build dense synthetic PhysicsSafeSet bands: sequences × tick windows × delay bands."""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from real_assist.dense import (
    DEFAULT_LIMIT,
    DEFAULT_WINDOWS,
    count_windowed_delay_probes,
    full_band,
    parse_windows,
    synthetic_physics_safe,
    synthetic_verifier_safe,
)
from real_assist.oracle import write_json

CORPUS = ROOT / "fixtures" / "real_corpus"
DENSE_ROOT = ROOT / "fixtures" / "real_assist" / "dense"
GOLDENS = DENSE_ROOT / "dense_goldens.json"


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def parse_family_k(family: str) -> tuple[str, int]:
    if family in ("on-time", "drop-click"):
        return family, 0
    parts = family.rsplit("-", 1)
    if len(parts) == 2 and parts[1].isdigit():
        return family, int(parts[1])
    return family, 0


def build_level(corpus_id: str, windows: list[int], primary_limit: int) -> dict:
    index_path = CORPUS / "derived" / corpus_id / "index.json"
    if not index_path.is_file():
        raise FileNotFoundError(
            f"missing {index_path}; run: python tools/shift_replay.py --dense --id {corpus_id}"
        )
    index = json.loads(index_path.read_text(encoding="utf-8"))
    out_dir = DENSE_ROOT / "oracles" / corpus_id
    out_dir.mkdir(parents=True, exist_ok=True)

    variants = []
    seq_counts: dict[int, int] = {}
    for row in index.get("variants") or []:
        family = str(row.get("family") or "")
        click_index = row.get("clickIndex")
        click_indices = list(row.get("clickIndices") or ([click_index] if click_index is not None else []))
        sequence_length = int(row.get("sequenceLength") or max(1, len(click_indices)))
        seq_counts[sequence_length] = seq_counts.get(sequence_length, 0) + 1
        _, k = parse_family_k(family)
        stem = Path(str(row.get("file") or "x.gdr2")).stem

        window_rows = []
        for window in windows:
            physics_safe = synthetic_physics_safe(family, k, limit=window)
            verifier_safe = synthetic_verifier_safe(physics_safe, family, k, limit=window)
            # Per-click safe sets for multi-press scoring (same synthetic family/k per click).
            per_click = [list(physics_safe) for _ in range(max(1, sequence_length))]
            window_rows.append(
                {
                    "limit": window,
                    "physicsSafe": physics_safe,
                    "verifierSafe": verifier_safe,
                    "perClickPhysicsSafe": per_click,
                }
            )

        primary = next((w for w in window_rows if w["limit"] == primary_limit), window_rows[-1])
        physics_name = f"{stem}.L{primary_limit}.physics_safe.json"
        verifier_name = f"{stem}.L{primary_limit}.verifier_safe.json"
        write_json(
            out_dir / physics_name,
            {
                "schema": 1,
                "kind": "PhysicsSafeSet",
                "claims": "ground-truth",
                "corpusId": corpus_id,
                "status": "ready",
                "family": family,
                "clickIndex": click_index,
                "clickIndices": click_indices,
                "sequenceLength": sequence_length,
                "fixture": "synthetic-dense",
                "limit": primary_limit,
                "safeDelays": primary["physicsSafe"],
                "band": full_band(primary["physicsSafe"], primary_limit),
                "windows": windows,
                "notes": "Synthetic dense band until PlayLayer physics probe replaces it.",
            },
        )
        write_json(
            out_dir / verifier_name,
            {
                "schema": 1,
                "kind": "VerifierSafeSet",
                "claims": "diagnostic",
                "corpusId": corpus_id,
                "status": "ready",
                "family": family,
                "clickIndex": click_index,
                "clickIndices": click_indices,
                "sequenceLength": sequence_length,
                "fixture": "synthetic-dense",
                "limit": primary_limit,
                "safeDelays": primary["verifierSafe"],
                "band": full_band(primary["verifierSafe"], primary_limit),
                "notes": "Diagnostic only.",
            },
        )
        variants.append(
            {
                "family": family,
                "clickIndex": click_index,
                "clickIndices": click_indices,
                "sequenceLength": sequence_length,
                "stem": stem,
                "limit": primary_limit,
                "physics": f"oracles/{corpus_id}/{physics_name}",
                "verifier": f"oracles/{corpus_id}/{verifier_name}",
                "physicsSafe": primary["physicsSafe"],
                "verifierSafe": primary["verifierSafe"],
                "windows": window_rows,
                "derivedFile": row.get("file"),
            }
        )

    probe_count = count_windowed_delay_probes(len(variants), windows)
    safe_points = 0
    for row in variants:
        for window_row in row.get("windows") or []:
            safe_points += len(window_row.get("physicsSafe") or [])

    return {
        "corpusId": corpus_id,
        "pairCount": index.get("pairCount"),
        "clickIndices": index.get("clickIndices"),
        "clickSpec": index.get("clickSpec"),
        "sequenceLengths": index.get("sequenceLengths"),
        "sequenceCounts": seq_counts,
        "variantCount": len(variants),
        "windows": windows,
        "limit": primary_limit,
        "delayProbeCount": probe_count,
        "safePointCount": safe_points,
        "variants": variants,
    }


def freeze(ids: list[str], windows: list[int], primary_limit: int) -> dict:
    levels = {}
    for corpus_id in ids:
        levels[corpus_id] = build_level(corpus_id, windows=windows, primary_limit=primary_limit)
    payload = {
        "schema": 1,
        "description": (
            "Dense Real Assist coverage: multi-click sequences x tick windows x delay bands. "
            "APR4 smoke (4 keys) remains the AP00 gate; this is the lab/CI density floor."
        ),
        "limit": primary_limit,
        "windows": windows,
        "minDelayProbesPerLevel": 200,
        "minSafePointsPerLevel": 100,
        "minSequenceVariants": { "2": 1, "3": 1 },
        "frozenAt": utc_now(),
        "levels": levels,
    }
    write_json(GOLDENS, payload)
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--id", action="append", dest="ids")
    parser.add_argument("--limit", type=int, default=DEFAULT_LIMIT, help="Primary window")
    parser.add_argument(
        "--windows",
        type=str,
        default=",".join(str(w) for w in DEFAULT_WINDOWS),
        help="Comma tick windows (default: 4,8,12,24)",
    )
    parser.add_argument(
        "--ensure-derived",
        action="store_true",
        help="Run shift_replay --dense when derived/index lacks multi-click sequences",
    )
    args = parser.parse_args()
    manifest_path = CORPUS / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    ids = args.ids or list(manifest.get("ids") or [])
    windows = parse_windows(args.windows)
    if args.limit not in windows:
        windows = sorted(set(windows) | {args.limit})

    if args.ensure_derived:
        from shift_replay import (
            DEFAULT_DENSE_CLICKS,
            DEFAULT_DENSE_SEQUENCES,
            DEFAULT_KS,
            SHIFT_FAMILIES,
            synthesize_id,
        )
        from real_assist.dense import parse_sequence_lengths

        for corpus_id in ids:
            index_path = CORPUS / "derived" / corpus_id / "index.json"
            need = True
            if index_path.is_file():
                index = json.loads(index_path.read_text(encoding="utf-8"))
                lengths = index.get("sequenceLengths") or []
                clicks = index.get("clickIndices") or []
                need = len(clicks) < 4 or 2 not in lengths or 3 not in lengths
            if not need:
                continue
            print(f"synthesizing dense+sequence derived for {corpus_id}...")
            synthesize_id(
                corpus_id,
                click_index=None,
                ks=DEFAULT_KS,
                families=SHIFT_FAMILIES,
                include_drop=True,
                click_spec=DEFAULT_DENSE_CLICKS,
                quiet=True,
                sequence_lengths=parse_sequence_lengths(DEFAULT_DENSE_SEQUENCES),
            )

    payload = freeze(ids, windows=windows, primary_limit=args.limit)
    for corpus_id, level in payload["levels"].items():
        print(
            f"{corpus_id}: variants={level['variantCount']} "
            f"seq={level.get('sequenceCounts')} "
            f"windows={level.get('windows')} "
            f"delayProbes={level['delayProbeCount']} "
            f"safePoints={level['safePointCount']}"
        )
    print(f"wrote {GOLDENS}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
