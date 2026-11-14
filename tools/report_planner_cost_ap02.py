"""Summarize AP02 timing fields from a Research Pack session."""
import json
import math
from pathlib import Path
import sys

class PlannerCostReport:
    @staticmethod
    def run(path):
        source = Path(path)
        if source.is_dir():
            source = source / "session.json"
        session = json.loads(source.read_text(encoding="utf-8-sig"))
        groups = {}
        for attempt in session["attempts"]:
            for event in attempt["events"]:
                if event["kind"] != "planFinished":
                    continue
                key = (event.get("gameModeKind"), event.get("plannerPath"))
                groups.setdefault(key, []).append(event)
        for key, events in groups.items():
            print(key, "inputs", len(events))
            for field in ("planUs", "worldCaptureUs", "prepareUs", "adaptiveWorkUs", "fallthroughSetupUs", "classicSearchUs", "adaptiveSeedCount"):
                values = sorted(e[field] for e in events if field in e)
                if not values:
                    print(field, "unavailable")
                    continue
                quantiles = [values[math.ceil(len(values) * q) - 1] for q in (.5, .95, .99)]
                print(field, "p50/p95/p99", *quantiles)

if __name__ == "__main__":
    PlannerCostReport.run(sys.argv[1])
