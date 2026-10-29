import json
from collections import Counter

p = r"c:\Users\mikaj\AppData\Local\GeometryDash\geode\mods\audesign.earlybuffer\devtraces\session-1793297951697.json"
with open(p, encoding="utf-8") as f:
    d = json.load(f)

print("schema", d.get("traceSchema"), "done", d.get("recordingsDone"), "target", d.get("recordingsTarget"))
print("attempts", len(d.get("attempts", [])))

for a in d.get("attempts", []):
    evs = a.get("events", [])
    kinds = Counter(e["kind"] for e in evs)
    presses = {}
    for e in evs:
        presses.setdefault(e.get("pressId", 0), []).append(e)
    print(
        f"  attempt {a['attemptId']} end={a['endReason']} sealed={a['sealed']} "
        f"events={len(evs)} kinds={dict(kinds)} presses={len([k for k in presses if k])}"
    )

# Focus last attempt with death and delayArm
last = None
for a in d["attempts"]:
    if a.get("endReason") == "death":
        last = a
if last is None:
    last = d["attempts"][-1]

print("\n=== FOCUS attempt", last["attemptId"], last["endReason"], "===")
evs = last["events"]
presses = {}
for e in evs:
    presses.setdefault(e.get("pressId", 0), []).append(e)

interesting = []
for pid, els in presses.items():
    if not pid:
        continue
    ks = [e["kind"] for e in els]
    if "delayArm" in ks or "fire" in ks:
        interesting.append((pid, els, ks))

print("arm/fire cycles:", len(interesting))
for pid, els, ks in interesting[-5:]:
    print(f"\n--- pressId {pid} chain={ks} ---")
    for e in els:
        keys = [
            "eventId",
            "kind",
            "parentEventId",
            "planId",
            "down",
            "effectTick",
            "pressTick",
            "plannedDelay",
            "plannedFireTick",
            "actualFireTick",
            "assistReason",
            "endReason",
        ]
        print({k: e.get(k) for k in keys})

# Validate parent chains for interesting cycles
print("\n=== chain checks (last 3 arm cycles) ===")
for pid, els, ks in interesting[-3:]:
    by_id = {e["eventId"]: e for e in els}
    ok = True
    msgs = []
    for e in els:
        if e["kind"] == "pressObserved":
            if e["parentEventId"] != 0:
                ok = False
                msgs.append("press parent!=0")
        else:
            if e["parentEventId"] not in by_id:
                ok = False
                msgs.append(f"parent missing for {e['kind']}")
            elif e["cycleId"] != e["pressId"]:
                ok = False
                msgs.append("cycle!=press")
    print(f"pressId {pid}: {'OK' if ok else 'BAD'} {msgs} keys={ks}")

# Last event overall in last death attempt
print("\n=== last 8 events of focus attempt ===")
for e in evs[-8:]:
    print(
        e["eventId"],
        e["kind"],
        "press",
        e.get("pressId"),
        "delay",
        e.get("plannedDelay"),
        "pFire",
        e.get("plannedFireTick"),
        "aFire",
        e.get("actualFireTick"),
        e.get("assistReason"),
    )
