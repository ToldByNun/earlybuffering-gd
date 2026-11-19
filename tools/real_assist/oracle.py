"""Dual oracle helpers (VerifierSafeSet diagnostic + PhysicsSafeSet ground truth)."""

from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def reproduce_status(corpus_dir: Path) -> str:
    path = corpus_dir / "reproduce.json"
    if not path.is_file():
        return "missing"
    data = load_json(path)
    return str(data.get("status") or "missing")


def require_reproduce_pass(corpus_dir: Path) -> None:
    status = reproduce_status(corpus_dir)
    if status != "pass":
        raise RuntimeError(
            f"oracle refused: {corpus_dir.name} reproduce={status} (need pass). "
            "No VerifierSafeSet / PhysicsSafeSet until gold PlayLayer clear passes."
        )


def empty_delay_band(limit: int) -> list[dict[str, Any]]:
    rows = []
    for delay in range(0, max(0, limit) + 1):
        rows.append(
            {
                "delay": delay,
                "survived": None,
                "tailComplete": None,
                "unknown": None,
                "budgetHit": None,
                "deathTick": None,
                "endPercent": None,
            }
        )
    return rows


def build_oracle_document(
    *,
    kind: str,
    corpus_id: str,
    sidecar: dict[str, Any],
    parent_sha: str,
    limit: int,
    settings_stamp: dict[str, Any],
    status: str,
    reason: str | None = None,
) -> dict[str, Any]:
    capture = sidecar.get("capture") or {}
    return {
        "schema": 1,
        "kind": kind,
        "claims": "diagnostic" if kind == "VerifierSafeSet" else "ground-truth",
        "corpusId": corpus_id,
        "status": status,
        "reason": reason,
        "family": sidecar.get("family"),
        "clickIndex": sidecar.get("clickIndex"),
        "parentSha256": parent_sha,
        "derivedFile": sidecar.get("derivedFile"),
        "sidecarCapture": capture,
        "clock": {
            "tps": sidecar.get("tps"),
            "assist": False,
            "practice": False,
            "startPos": False,
        },
        "settingsStamp": settings_stamp,
        "limit": limit,
        "safeDelays": [],
        "band": empty_delay_band(limit),
        "updatedAt": utc_now(),
        "notes": (
            "VerifierSafeSet: shadow verifyDelay at pre-shifted-press state (diagnostic only). "
            "PhysicsSafeSet: real PlayLayer delayed inject from the same state (acceptance)."
            if status == "pending-probe"
            else reason
        ),
    }
