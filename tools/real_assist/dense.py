"""Dense Real Assist coverage: multi-click samples × sequences × tick windows × delay bands."""

from __future__ import annotations

from typing import Any

# Assist delay search widths (ticks-all-ish). Product window default is 4; lab covers several.
DEFAULT_LIMIT = 12
DEFAULT_WINDOWS = [4, 8, 12, 24]
# Consecutive click lengths for multi-press / smart-depth style tests.
DEFAULT_SEQUENCE_LENGTHS = [1, 2, 3]


def quantile_click_indices(pair_count: int, samples: int) -> list[int]:
    """Evenly spaced click indices across the clear (inclusive ends when samples>=2)."""
    if pair_count <= 0:
        return []
    if samples <= 1:
        return [pair_count // 2]
    samples = min(samples, pair_count)
    if samples == pair_count:
        return list(range(pair_count))
    out: list[int] = []
    for i in range(samples):
        index = int(round(i * (pair_count - 1) / (samples - 1)))
        if not out or out[-1] != index:
            out.append(index)
    return out


def every_n_click_indices(pair_count: int, step: int) -> list[int]:
    if pair_count <= 0 or step <= 0:
        return []
    return list(range(0, pair_count, step))


def parse_click_spec(spec: str, pair_count: int) -> list[int]:
    """
    Specs:
      middle          -> single middle click
      all             -> every click (huge)
      quantile:N      -> N evenly spaced clicks
      every:N         -> 0, N, 2N, ...
      list:1,2,3      -> explicit indices
    """
    text = (spec or "middle").strip().lower()
    if text in ("middle", "mid", "default"):
        return [pair_count // 2] if pair_count else []
    if text == "all":
        return list(range(pair_count))
    if text.startswith("quantile:"):
        n = int(text.split(":", 1)[1])
        return quantile_click_indices(pair_count, n)
    if text.startswith("every:"):
        step = int(text.split(":", 1)[1])
        return every_n_click_indices(pair_count, step)
    if text.startswith("list:"):
        return [int(part) for part in text.split(":", 1)[1].split(",") if part.strip()]
    raise ValueError(f"unknown click spec: {spec}")


def parse_sequence_lengths(spec: str | None) -> list[int]:
    if not spec:
        return list(DEFAULT_SEQUENCE_LENGTHS)
    lengths = sorted({int(part) for part in spec.split(",") if part.strip()})
    if not lengths or any(length < 1 for length in lengths):
        raise ValueError(f"bad sequence lengths: {spec}")
    return lengths


def parse_windows(spec: str | None) -> list[int]:
    if not spec:
        return list(DEFAULT_WINDOWS)
    windows = sorted({int(part) for part in spec.split(",") if part.strip()})
    if not windows or any(window < 0 for window in windows):
        raise ValueError(f"bad windows: {spec}")
    return windows


def sequence_starts(anchors: list[int], pair_count: int, length: int) -> list[int]:
    """Anchor starts where [start, start+length) fits inside the clear."""
    if length <= 1:
        return list(anchors)
    out = []
    for start in anchors:
        if start < 0 or start + length > pair_count:
            continue
        out.append(start)
    return out


def synthetic_physics_safe(family: str, k: int, limit: int = DEFAULT_LIMIT) -> list[int]:
    """
    Deterministic stand-in until PlayLayer fills real bands.
    Early-k: safe around delay≈k. Late-k: safe around small delays.
    on-time: early delays. drop-click: empty (invent lane).
    """
    limit = max(0, limit)
    if family == "drop-click" or family.startswith("drop-click"):
        return []
    if family == "on-time" or family.startswith("on-time"):
        return [d for d in range(0, min(3, limit) + 1)]
    if "early" in family:
        center = max(0, min(limit, k))
        return sorted(
            {
                max(0, center - 1),
                center,
                min(limit, center + 1),
            }
        )
    if "late" in family:
        return [d for d in range(0, min(2, limit) + 1)]
    return [0]


def synthetic_verifier_safe(
    physics_safe: list[int], family: str, k: int, limit: int = DEFAULT_LIMIT
) -> list[int]:
    """Slightly wider than physics for diagnostic disagreement coverage on early shifts."""
    base = set(physics_safe)
    if "early" in family and physics_safe:
        widest = min(limit, max(physics_safe) + 1)
        base.add(widest)
    if family == "on-time" or family.startswith("on-time"):
        return sorted(base)
    return sorted(d for d in base if 0 <= d <= limit)


def full_band(safe: list[int], limit: int = DEFAULT_LIMIT) -> list[dict[str, Any]]:
    safe_set = set(safe)
    rows = []
    for delay in range(0, limit + 1):
        ok = delay in safe_set
        rows.append(
            {
                "delay": delay,
                "survived": ok,
                "tailComplete": ok,
                "unknown": False,
                "budgetHit": False,
            }
        )
    return rows


def delay_sweep_presses(limit: int) -> list[dict[str, Any]]:
    return [{"plannedDelay": d, "planUs": 1000 + d, "assisted": True} for d in range(0, limit + 1)]


def sequence_presses(
    per_click_safe: list[list[int]], *, pick: str = "center"
) -> list[dict[str, Any]]:
    """Build a multi-press session from per-click PhysicsSafeSets."""
    presses = []
    for index, safe in enumerate(per_click_safe):
        if not safe:
            delay = 0
        elif pick == "min":
            delay = min(safe)
        elif pick == "max":
            delay = max(safe)
        else:
            delay = safe[len(safe) // 2]
        presses.append(
            {
                "plannedDelay": delay,
                "planUs": 1100 + 50 * index,
                "assisted": True,
                "clickOrdinal": index,
                "physicsSafe": list(safe),
            }
        )
    return presses


def score_sequence_presses(presses: list[dict[str, Any]]) -> dict[str, float]:
    """Per-press PhysicsHit / Invent using optional press['physicsSafe'] lists."""
    if not presses:
        return {"PhysicsHitRate": 0.0, "InventRate": 0.0, "presses": 0.0}
    hits = 0
    invent = 0
    for press in presses:
        delay = int(press.get("plannedDelay") or 0)
        safe = {int(x) for x in (press.get("physicsSafe") or [])}
        if delay in safe:
            hits += 1
        else:
            invent += 1
    n = len(presses)
    return {
        "PhysicsHitRate": hits / n,
        "InventRate": invent / n,
        "presses": float(n),
    }


def count_delay_probes(variant_count: int, limit: int) -> int:
    return variant_count * (limit + 1)


def count_windowed_delay_probes(variant_count: int, windows: list[int]) -> int:
    return sum(variant_count * (window + 1) for window in windows)


def count_safe_points(variants: list[dict[str, Any]]) -> int:
    total = 0
    for row in variants:
        total += len(row.get("physicsSafe") or [])
        for window_row in row.get("windows") or []:
            # Avoid double-count: prefer top-level physicsSafe for primary limit only.
            if window_row.get("limit") == row.get("limit"):
                continue
            total += len(window_row.get("physicsSafe") or [])
    return total
