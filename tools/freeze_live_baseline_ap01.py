"""Freeze and validate the November 15 Live baseline without inferring replay parity."""
import argparse
import hashlib
import json
import os
from collections import Counter
from pathlib import Path


class LiveBaseline:
    stamps = ("012358", "012700", "012752", "012903")
    fixture = Path(__file__).parent / "fixtures" / "live_baseline_170_ap01.json"

    @classmethod
    def freeze(cls, root):
        packs = []
        for stamp in cls.stamps:
            directory = root / ("research_20261115_" + stamp)
            files = {}
            for name in ("session.json", "config.json", "report.json", "MANIFEST.json"):
                raw = (directory / name).read_bytes()
                files[name] = {
                    "sha256": hashlib.sha256(raw).hexdigest(),
                    "data": json.loads(raw),
                }
            packs.append({"stamp": directory.name, "files": files})
        cls.fixture.write_text(json.dumps({"schema": 1, "packs": packs}, indent=2) + "\n",
                               encoding="utf-8")

    @classmethod
    def validate(cls):
        baseline = json.loads(cls.fixture.read_text(encoding="utf-8"))
        expected = {"012358": 49, "012700": 35, "012752": 39, "012903": 57}
        for pack in baseline["packs"]:
            session = pack["files"]["session.json"]["data"]
            count = 0
            reasons = Counter()
            for attempt in session["attempts"]:
                events = attempt["events"]
                identifiers = [event["eventId"] for event in events]
                assert len(identifiers) == len(set(identifiers))
                for event in events:
                    if event["kind"] != "planFinished":
                        continue
                    count += 1
                    reasons[event["assistReason"]] += 1
                    assert event["attemptId"] == attempt["attemptId"]
                    assert "playerIndex" in event and "clock" in event
                    assert event["plannedDelay"] >= 0
                evaluation = attempt.get("eval")
                if evaluation and evaluation.get("valid"):
                    assert evaluation["fidelity"] == "playerOnlyApproximated"
            assert count == expected[pack["stamp"][-6:]]
            print(pack["stamp"], count, dict(reasons))
        print("Frozen evidence validated. Counterfactual replay parity remains unresolved.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--freeze", action="store_true")
    parser.add_argument("--research-root", type=Path, default=Path(
        os.environ.get("LOCALAPPDATA", ".")) /
        "GeometryDash/geode/mods/audesign.earlybuffer/research")
    arguments = parser.parse_args()
    if arguments.freeze:
        LiveBaseline.freeze(arguments.research_root)
    LiveBaseline.validate()
