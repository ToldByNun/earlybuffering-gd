"""APR3 offline scorer — PhysicsSafeSet is acceptance; VerifierSafeSet is diagnostic only."""

from __future__ import annotations

import math
import statistics
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

from .oracle import load_json


def safe_delay_set(oracle: dict[str, Any] | None) -> set[int]:
    if not oracle:
        return set()
    explicit = oracle.get("safeDelays")
    if isinstance(explicit, list) and explicit:
        return {int(x) for x in explicit}
    band = oracle.get("band") or []
    out: set[int] = set()
    for row in band:
        if not isinstance(row, dict):
            continue
        if row.get("survived") and row.get("tailComplete"):
            if row.get("unknown") or row.get("budgetHit"):
                continue
            if "delay" in row:
                out.add(int(row["delay"]))
    return out


def collect_planned_delays(session: dict[str, Any]) -> list[int]:
    delays: list[int] = []
    for press in session.get("presses") or []:
        if not isinstance(press, dict):
            continue
        if "plannedDelay" not in press:
            continue
        delays.append(int(press["plannedDelay"]))
    if delays:
        return delays
    # Fallback: attempt-level plannedDelay if present.
    for attempt in session.get("attempts") or []:
        if not isinstance(attempt, dict):
            continue
        if "plannedDelay" in attempt:
            delays.append(int(attempt["plannedDelay"]))
        for delay in attempt.get("plannedDelays") or []:
            delays.append(int(delay))
    return delays


def collect_plan_us(session: dict[str, Any]) -> list[float]:
    values: list[float] = []
    for press in session.get("presses") or []:
        if isinstance(press, dict) and press.get("planUs") is not None:
            values.append(float(press["planUs"]))
    for attempt in session.get("attempts") or []:
        if not isinstance(attempt, dict):
            continue
        if attempt.get("avgPlanUs") is not None:
            values.append(float(attempt["avgPlanUs"]))
        for plan_us in attempt.get("planUsSamples") or []:
            values.append(float(plan_us))
    return values


def percentile(values: list[float], p: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    rank = (len(ordered) - 1) * p
    low = math.floor(rank)
    high = math.ceil(rank)
    if low == high:
        return ordered[low]
    return ordered[low] * (high - rank) + ordered[high] * (rank - low)


@dataclass
class ScoreResult:
    schema: int = 1
    corpusId: str = ""
    variant: str = ""
    mode: str = ""
    protocol: str = ""
    SurviveRate: float | None = None
    PhysicsHitRate: float | None = None
    VerifierHitRate: float | None = None
    DAF_exposed: float | None = None
    InventRate: float | None = None
    AssistCov: float | None = None
    arms: int = 0
    attempts: int = 0
    completes: int = 0
    plannedDelayCount: int = 0
    physicsHits: int = 0
    verifierHits: int = 0
    inventCount: int = 0
    planUs_p50: float | None = None
    planUs_p95: float | None = None
    budgetHitRate: float | None = None
    ColdStartAttempts: int | None = None
    physicsSafeAvailable: bool = False
    verifierSafeAvailable: bool = False
    verifierPhysicsDisagreements: list[int] = field(default_factory=list)
    diagnosticFailures: list[str] = field(default_factory=list)
    hardFail: bool = False
    hardFailReasons: list[str] = field(default_factory=list)
    acceptanceUses: str = "PhysicsHitRate"  # never VerifierHitRate

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


def score_session(
    session: dict[str, Any],
    *,
    physics_oracle: dict[str, Any] | None = None,
    verifier_oracle: dict[str, Any] | None = None,
) -> ScoreResult:
    result = ScoreResult(
        corpusId=str(session.get("corpusId") or ""),
        variant=str(session.get("variant") or ""),
        mode=str(session.get("mode") or ""),
        protocol=str(session.get("protocol") or ""),
    )

    attempts = [a for a in (session.get("attempts") or []) if isinstance(a, dict)]
    result.attempts = len(attempts)
    result.completes = sum(1 for a in attempts if a.get("completed"))
    if result.attempts:
        result.SurviveRate = result.completes / result.attempts
    elif session.get("SurviveRate") is not None:
        result.SurviveRate = float(session["SurviveRate"])

    arms = 0
    daf = 0
    n_opp = 0
    budget_hits = []
    for attempt in attempts:
        arms += int(attempt.get("arms") or 0)
        daf += int(attempt.get("daf") or 0)
        n_opp += int(attempt.get("nOpp") or 0)
        if attempt.get("budgetHitRate") is not None:
            budget_hits.append(float(attempt["budgetHitRate"]))
    result.arms = arms
    if session.get("DAF_exposed") is not None and not attempts:
        result.DAF_exposed = float(session["DAF_exposed"])
    else:
        result.DAF_exposed = daf / max(1, arms)
    if n_opp > 0:
        result.AssistCov = arms / n_opp
    if budget_hits:
        result.budgetHitRate = sum(budget_hits) / len(budget_hits)

    plan_us = collect_plan_us(session)
    result.planUs_p50 = percentile(plan_us, 0.50)
    result.planUs_p95 = percentile(plan_us, 0.95)

    if str(result.protocol).lower() == "online":
        # First attempt index where a physics hit occurred, if stamped.
        cold = session.get("ColdStartAttempts")
        if cold is not None:
            result.ColdStartAttempts = int(cold)
        else:
            for attempt in attempts:
                if attempt.get("physicsHit"):
                    result.ColdStartAttempts = int(attempt.get("index") or 0) + 1
                    break

    physics_safe = safe_delay_set(physics_oracle)
    verifier_safe = safe_delay_set(verifier_oracle)
    result.physicsSafeAvailable = physics_oracle is not None and (
        bool(physics_safe) or physics_oracle.get("status") == "ready"
    )
    result.verifierSafeAvailable = verifier_oracle is not None and (
        bool(verifier_safe) or verifier_oracle.get("status") == "ready"
    )

    # Disagreement: membership differs for any delay that appears in either set.
    if physics_oracle is not None and verifier_oracle is not None:
        for delay in sorted(physics_safe | verifier_safe):
            in_p = delay in physics_safe
            in_v = delay in verifier_safe
            if in_p != in_v:
                result.verifierPhysicsDisagreements.append(delay)
        if result.verifierPhysicsDisagreements:
            result.diagnosticFailures.append(
                "verifier-physics-disagreement delays="
                + ",".join(str(d) for d in result.verifierPhysicsDisagreements)
            )

    delays = collect_planned_delays(session)
    result.plannedDelayCount = len(delays)
    if delays and physics_safe:
        hits = sum(1 for delay in delays if delay in physics_safe)
        result.physicsHits = hits
        result.PhysicsHitRate = hits / len(delays)
        # Invent = assisted planned delay outside PhysicsSafeSet (hard gate target 0).
        invent = sum(1 for delay in delays if delay not in physics_safe)
        result.inventCount = invent
        result.InventRate = invent / len(delays)
    elif session.get("PhysicsHitRate") is not None and float(session["PhysicsHitRate"]) >= 0:
        result.PhysicsHitRate = float(session["PhysicsHitRate"])
    elif physics_oracle is None:
        result.diagnosticFailures.append("physics-oracle-missing")

    if delays and verifier_safe:
        vhits = sum(1 for delay in delays if delay in verifier_safe)
        result.verifierHits = vhits
        result.VerifierHitRate = vhits / len(delays)
    elif session.get("VerifierHitRate") is not None and float(session["VerifierHitRate"]) >= 0:
        result.VerifierHitRate = float(session["VerifierHitRate"])

    # Hard gates (CI): InventRate > 0 when measurable; never use VerifierHit for acceptance.
    if result.InventRate is not None and result.InventRate > 0:
        result.hardFail = True
        result.hardFailReasons.append(f"InventRate={result.InventRate:.3f}>0")
    if result.verifierPhysicsDisagreements:
        # Diagnostic failure — does not set hardFail by itself (acceptance is physics).
        pass

    return result


def load_oracle_beside(session_path: Path, kind: str) -> dict[str, Any] | None:
    """Look for Physics/Verifier safe JSONs next to sessions or under fixtures."""
    session = load_json(session_path)
    corpus_id = session.get("corpusId")
    variant = session.get("variant")
    if not corpus_id or not variant:
        return None
    suffix = ".physics_safe.json" if kind == "physics" else ".verifier_safe.json"
    tools = Path(__file__).resolve().parent.parent
    candidates = [
        session_path.parent / f"{variant}{suffix}",
        session_path.parent / f"{corpus_id}_{variant}{suffix}",
        session_path.parent.parent / "oracles" / f"{corpus_id}_{variant}{suffix}",
        tools / "fixtures" / "real_assist" / "oracles" / f"{corpus_id}_{variant}{suffix}",
        tools
        / "fixtures"
        / "real_corpus"
        / "derived"
        / str(corpus_id)
        / f"{variant}{suffix}",
        Path(session.get("physicsSafePath") or "")
        if kind == "physics"
        else Path(session.get("verifierSafePath") or ""),
    ]
    # derived/<id>/variant*.physics_safe.json
    derived = tools / "fixtures" / "real_corpus" / "derived" / str(corpus_id)
    if derived.is_dir():
        candidates.extend(sorted(derived.glob(f"{variant}*{suffix}")))
    save_derived = session_path.parent.parent / "real_corpus" / "derived" / str(corpus_id)
    if save_derived.is_dir():
        candidates.extend(sorted(save_derived.glob(f"{variant}*{suffix}")))

    for path in candidates:
        if path and path.is_file():
            return load_json(path)
    return None


def score_session_file(
    session_path: Path,
    *,
    physics_path: Path | None = None,
    verifier_path: Path | None = None,
) -> ScoreResult:
    session = load_json(session_path)
    physics = load_json(physics_path) if physics_path else load_oracle_beside(session_path, "physics")
    verifier = (
        load_json(verifier_path) if verifier_path else load_oracle_beside(session_path, "verifier")
    )
    return score_session(session, physics_oracle=physics, verifier_oracle=verifier)
