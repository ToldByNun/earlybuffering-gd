"""Helpers shared by compare_modes CLI and APR3 golden tests."""

from __future__ import annotations

from collections import defaultdict
from pathlib import Path
from typing import Any

from .oracle import load_json
from .score import ScoreResult, score_session


def _row(score: ScoreResult) -> dict[str, Any]:
    return {
        "mode": score.mode,
        "SurviveRate": score.SurviveRate,
        "PhysicsHitRate": score.PhysicsHitRate,
        "VerifierHitRate": score.VerifierHitRate,
        "InventRate": score.InventRate,
        "DAF_exposed": score.DAF_exposed,
        "planUs_p50": score.planUs_p50,
        "planUs_p95": score.planUs_p95,
        "hardFail": score.hardFail,
        "diagnosticFailures": score.diagnosticFailures,
        "acceptanceUses": score.acceptanceUses,
    }


def compare(scores: list[ScoreResult]) -> dict[str, Any]:
    by_protocol: dict[str, list[ScoreResult]] = defaultdict(list)
    for score in scores:
        by_protocol[score.protocol or "Unknown"].append(score)

    sections: dict[str, Any] = {}
    for protocol, rows in sorted(by_protocol.items()):
        groups: dict[tuple[str, str], dict[str, ScoreResult]] = defaultdict(dict)
        for score in rows:
            groups[(score.corpusId, score.variant)][score.mode] = score

        comparisons = []
        for (corpus_id, variant), modes in sorted(groups.items()):
            live = modes.get("Live")
            hybrid = modes.get("Hybrid")
            entry: dict[str, Any] = {
                "corpusId": corpus_id,
                "variant": variant,
                "Live": _row(live) if live else None,
                "Hybrid": _row(hybrid) if hybrid else None,
                "notes": [],
            }
            if live and hybrid:
                if live.PhysicsHitRate is not None and hybrid.PhysicsHitRate is not None:
                    entry["physicsHitDelta_HybridMinusLive"] = (
                        hybrid.PhysicsHitRate - live.PhysicsHitRate
                    )
                if live.SurviveRate is not None and hybrid.SurviveRate is not None:
                    entry["surviveDelta_HybridMinusLive"] = (
                        hybrid.SurviveRate - live.SurviveRate
                    )
                if live.InventRate and live.InventRate > 0:
                    entry["notes"].append("Live InventRate>0")
                if hybrid.InventRate and hybrid.InventRate > 0:
                    entry["notes"].append("Hybrid InventRate>0")
                for score in (live, hybrid):
                    if score.verifierPhysicsDisagreements:
                        entry["notes"].append(f"{score.mode} verifier-physics-disagreement")
            comparisons.append(entry)

        sections[protocol] = {
            "label": protocol,
            "separated": True,
            "note": (
                "Frozen and Online must never be mixed in one acceptance table."
                if protocol in ("Frozen", "Online")
                else "Unknown protocol — treat as unlabeled."
            ),
            "cases": comparisons,
        }

    return {
        "schema": 1,
        "acceptanceMetric": "PhysicsHitRate",
        "diagnosticMetric": "VerifierHitRate",
        "sections": sections,
        "hardFailAny": any(score.hardFail for score in scores),
    }


def build_compare_from_goldens(fixture_root: Path, goldens: dict[str, Any]) -> dict[str, Any]:
    scores: list[ScoreResult] = []
    for case in goldens.get("cases") or []:
        if "invent" in str(case.get("name") or ""):
            continue
        session = load_json(fixture_root / case["session"])
        physics = load_json(fixture_root / case["physics"])
        verifier = load_json(fixture_root / case["verifier"])
        scores.append(
            score_session(session, physics_oracle=physics, verifier_oracle=verifier)
        )
    return compare(scores)
