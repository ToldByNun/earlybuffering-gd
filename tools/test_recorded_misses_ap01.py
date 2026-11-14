"""Freeze AP01 evidence and verify recorded survival windows without a running GD."""
import hashlib
import json
import os
from pathlib import Path
import sys


class RecordedMissRegression:
    cases = [
        ("011029", 12, "ufo-miss", list(range(3, 10))),
        ("010857", 2, "wave-wrong-delay", list(range(4, 9))),
        ("010622", 6, "ball-control", [6]),
        ("010741", 4, "cube-control", [3, 4]),
        ("010741", 6, "cube-control-late", [10, 11, 12]),
    ]
    fixture = Path(__file__).parent / "fixtures" / "recorded_misses_ap01.json"

    @classmethod
    def freeze(cls):
        root = Path(os.environ["LOCALAPPDATA"]) / "GeometryDash/geode/mods/audesign.earlybuffer/research"
        records = []
        for stamp, attempt_id, name, expected in cls.cases:
            source = root / ("research_20261114_" + stamp) / "session.json"
            raw = source.read_bytes()
            session = json.loads(raw)
            attempt = next(a for a in session["attempts"] if a["attemptId"] == attempt_id)
            records.append(dict(name=name, source=str(source), sha256=hashlib.sha256(raw).hexdigest(),
                                modVersion=session["modVersion"], levelKey=session["levelKey"],
                                sessionId=session["sessionId"], expected=expected, attempt=attempt))
        cls.fixture.write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")

    @classmethod
    def run(cls):
        for record in json.loads(cls.fixture.read_text(encoding="utf-8")):
            evaluation = record["attempt"]["eval"]
            assert evaluation["fidelity"] == "reproduced", record["name"]
            assert evaluation["claimsAllowed"] and not evaluation["objectivePartial"]
            safe = [
                sample["delay"] for sample in evaluation["samples"]
                if sample["survivedPrimary"] and sample["tailComplete"]
                and not any(sample.get(key) for key in ("tailFailed", "unknown", "budgetHit", "aborted"))
            ]
            assert safe == record["expected"], (record["name"], safe)
            chosen = evaluation["plannedDelay"]
            if "control" in record["name"]:
                assert chosen in safe
            else:
                assert chosen not in safe
            print(record["name"], "recorded", chosen, "verified window", safe)
        print("Recorded evidence verified. Live planner repair remains pending.")


if __name__ == "__main__":
    if "--freeze" in sys.argv:
        RecordedMissRegression.freeze()
    RecordedMissRegression.run()
