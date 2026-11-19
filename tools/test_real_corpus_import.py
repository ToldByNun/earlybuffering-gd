"""APR0 import sanity: all four gold clears parse with events + TPS."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from real_assist.gdr2 import parse_gdr2

FIXTURE_ROOT = ROOT / "fixtures" / "real_corpus"
EXPECTED_IDS = ("cataclysm", "quantum", "slaughterhouse", "hoveropolis")


def run() -> None:
    manifest_path = FIXTURE_ROOT / "manifest.json"
    if not manifest_path.is_file():
        raise SystemExit(
            f"missing {manifest_path}; run: python tools/sync_real_corpus.py"
        )
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    ids = manifest.get("ids") or []
    if list(ids) != list(EXPECTED_IDS):
        raise AssertionError(f"manifest ids {ids} != {list(EXPECTED_IDS)}")

    for corpus_id in EXPECTED_IDS:
        folder = FIXTURE_ROOT / corpus_id
        clear = folder / "clear.gdr2"
        meta_path = folder / "meta.json"
        reproduce_path = folder / "reproduce.json"
        if not clear.is_file():
            raise AssertionError(f"{corpus_id}: missing clear.gdr2 (run sync)")
        if not meta_path.is_file():
            raise AssertionError(f"{corpus_id}: missing meta.json")
        if not reproduce_path.is_file():
            raise AssertionError(f"{corpus_id}: missing reproduce.json")

        replay = parse_gdr2(clear)
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
        reproduce = json.loads(reproduce_path.read_text(encoding="utf-8"))

        assert replay.framerate > 0, corpus_id
        assert len(replay.inputs) > 0, corpus_id
        assert meta["clear"]["eventCount"] == len(replay.inputs), corpus_id
        assert abs(meta["clear"]["tps"] - replay.framerate) < 1e-9, corpus_id
        assert reproduce["corpusId"] == corpus_id, corpus_id
        assert reproduce["status"] in ("pending", "pass", "fail"), corpus_id
        assert reproduce["import"]["eventCount"] == len(replay.inputs), corpus_id
        assert reproduce["clearSha256"] == meta["clear"]["sha256"], corpus_id

        p2 = sum(1 for item in replay.inputs if item.player2)
        print(
            f"{corpus_id}: OK events={len(replay.inputs)} tps={replay.framerate} "
            f"dual={p2 > 0} reproduce={reproduce['status']} "
            f"level={replay.level_name!r}"
        )

    print("Real corpus import sanity OK.")


if __name__ == "__main__":
    run()
