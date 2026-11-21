"""Sync local gold clears into tools/fixtures/real_corpus/<id>/.

Source default: EB_REAL_CORPUS_SRC or C:\\Users\\mikaj\\Music\\earlybuffer
Optional: --install-save copies into the Geode mod save dir for in-game runners.
"""

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

from real_assist.gdr2 import parse_gdr2

FIXTURE_ROOT = ROOT / "fixtures" / "real_corpus"
DEFAULT_SRC = Path(r"C:\Users\mikaj\Music\earlybuffer")

CORPUS = [
    {
        "id": "cataclysm",
        "level": "Cataclysm 2.gmd",
        "clear": "cataclysm.gdr2",
        "tags": ["dual", "varied-gamemodes", "fast"],
    },
    {
        "id": "quantum",
        "level": "Quantum Processing 2.gmd",
        "clear": "quantum processing.gdr2",
        "tags": ["dual", "varied-gamemodes"],
    },
    {
        "id": "slaughterhouse",
        "level": "Slaughterhouse  2.gmd",
        "clear": "slaughterhouse.gdr2",
        "tags": ["wave", "cube", "fast"],
    },
    {
        "id": "hoveropolis",
        "level": "Silent Hoveropolis.gmd",
        "clear": "silent hoveopolis.gdr2",
        "tags": ["ship", "ball", "cube", "tight"],
    },
]

CLOCK = {
    "practice": False,
    "startPos": False,
    "assist": False,
    "percentStart": 0,
    "sameTickOrder": ["frame", "player2", "button", "down"],
    "notes": "frames↔ticks via bake prepareBakeTimeline / unmapTick; live measured dt",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def default_save_dir() -> Path:
    local = os.environ.get("LOCALAPPDATA")
    if not local:
        raise SystemExit("LOCALAPPDATA unset; pass --install-save PATH")
    return Path(local) / "GeometryDash" / "geode" / "mods" / "audesign.earlybuffer" / "real_corpus"


def write_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def sync_one(src: Path, entry: dict, install_root: Path | None) -> dict:
    level_src = src / entry["level"]
    clear_src = src / entry["clear"]
    if not level_src.is_file():
        raise FileNotFoundError(f"missing level: {level_src}")
    if not clear_src.is_file():
        raise FileNotFoundError(f"missing clear: {clear_src}")

    dest = FIXTURE_ROOT / entry["id"]
    dest.mkdir(parents=True, exist_ok=True)
    level_dest = dest / "level.gmd"
    clear_dest = dest / "clear.gdr2"
    shutil.copy2(level_src, level_dest)
    shutil.copy2(clear_src, clear_dest)

    if install_root is not None:
        install_dest = install_root / entry["id"]
        install_dest.mkdir(parents=True, exist_ok=True)
        shutil.copy2(level_dest, install_dest / "level.gmd")
        shutil.copy2(clear_dest, install_dest / "clear.gdr2")

    replay = parse_gdr2(clear_dest)
    p1 = sum(1 for item in replay.inputs if not item.player2)
    p2 = sum(1 for item in replay.inputs if item.player2)
    last_frame = replay.inputs[-1].frame if replay.inputs else 0
    clear_sha = sha256_file(clear_dest)
    level_sha = sha256_file(level_dest)

    meta = {
        "schema": 1,
        "id": entry["id"],
        "levelFile": "level.gmd",
        "clearFile": "clear.gdr2",
        "tags": entry["tags"],
        "source": {
            "level": entry["level"],
            "clear": entry["clear"],
            "syncedAt": utc_now(),
        },
        "level": {
            "name": replay.level_name,
            "id": replay.level_id,
            "sha256": level_sha,
            "bytes": level_dest.stat().st_size,
        },
        "clear": {
            "sha256": clear_sha,
            "bytes": clear_dest.stat().st_size,
            "author": replay.author,
            "bot": replay.bot_name,
            "botVersion": replay.bot_version,
            "gameVersion": replay.game_version,
            "tps": replay.framerate,
            "eventCount": len(replay.inputs),
            "p1Events": p1,
            "p2Events": p2,
            "dual": p2 > 0,
            "platformer": replay.platformer,
            "lastFrame": last_frame,
            "deathCount": len(replay.deaths),
            "durationHeader": replay.duration,
        },
        "clock": {
            **CLOCK,
            "tps": replay.framerate,
        },
    }
    write_json(dest / "meta.json", meta)
    if install_root is not None:
        write_json(install_root / entry["id"] / "meta.json", meta)

    reproduce_path = dest / "reproduce.json"
    previous = None
    if reproduce_path.is_file():
        previous = json.loads(reproduce_path.read_text(encoding="utf-8"))
    status = "pending"
    attempt = None
    if (
        isinstance(previous, dict)
        and previous.get("clearSha256") == clear_sha
        and previous.get("status") in ("pass", "fail", "pending")
    ):
        status = previous["status"]
        attempt = previous.get("attempt")

    reproduce = {
        "schema": 1,
        "corpusId": entry["id"],
        "status": status,
        "clearSha256": clear_sha,
        "clock": meta["clock"],
        "import": {
            "tps": replay.framerate,
            "eventCount": len(replay.inputs),
            "lastFrame": last_frame,
            "dual": p2 > 0,
        },
        "attempt": attempt,
        "notes": (
            "Untrusted for oracle until status=pass from an unmodified PlayLayer gold clear "
            "(assist off, practice/startpos off unless tagged)."
        ),
        "updatedAt": utc_now(),
    }
    write_json(reproduce_path, reproduce)
    if install_root is not None:
        write_json(install_root / entry["id"] / "reproduce.json", reproduce)

    return {
        "id": entry["id"],
        "tags": entry["tags"],
        "levelSha256": level_sha,
        "clearSha256": clear_sha,
        "tps": replay.framerate,
        "eventCount": len(replay.inputs),
        "dual": p2 > 0,
        "levelName": replay.level_name,
        "levelId": replay.level_id,
        "reproduce": status,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--src",
        type=Path,
        default=Path(os.environ.get("EB_REAL_CORPUS_SRC", str(DEFAULT_SRC))),
        help="Folder with the four .gmd + .gdr2 pairs",
    )
    parser.add_argument(
        "--install-save",
        nargs="?",
        const="",
        default=None,
        help="Also copy into mod save real_corpus/ (default path if flag given with no value)",
    )
    args = parser.parse_args()
    src = args.src
    if not src.is_dir():
        print(f"source missing: {src}", file=sys.stderr)
        return 1

    install_root = None
    if args.install_save is not None:
        install_root = Path(args.install_save) if args.install_save else default_save_dir()
        install_root.mkdir(parents=True, exist_ok=True)

    FIXTURE_ROOT.mkdir(parents=True, exist_ok=True)
    (FIXTURE_ROOT / "derived").mkdir(parents=True, exist_ok=True)

    entries = []
    for item in CORPUS:
        summary = sync_one(src, item, install_root)
        entries.append(summary)
        print(
            f"{summary['id']}: events={summary['eventCount']} tps={summary['tps']} "
            f"dual={summary['dual']} reproduce={summary['reproduce']}"
        )

    manifest = {
        "schema": 1,
        "syncedAt": utc_now(),
        "source": str(src),
        "ids": [item["id"] for item in entries],
        "entries": entries,
        "gate": {
            "oracleRequires": "reproduce=pass",
            "assistOnReproduce": False,
        },
    }
    write_json(FIXTURE_ROOT / "manifest.json", manifest)
    if install_root is not None:
        write_json(install_root / "manifest.json", manifest)
        print(f"installed save corpus -> {install_root}")

    print(f"fixture root -> {FIXTURE_ROOT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
