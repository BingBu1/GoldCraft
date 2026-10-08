"""Observe real ZP round/model changes and main-view culling in sandbox B.

Only renderer diagnostics and the dedicated server's round restart are changed.
No mouse/keyboard, camera, player movement or bot targets are synthesized.
"""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parent.parent
CLIENT = ROOT / "sandbox/cs-client-b"
DIAGNOSTICS = CLIENT / "Half-Life/cstrike/renderer/studio-diagnostics.json"
SERVER = ROOT / "sandbox/cs-server/Half-Life/cstrike/addons/amxmodx/logs/goldcraft-zombie.json"


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def read_json(path):
    for _ in range(20):
        try:
            return json.loads(path.read_text(encoding="utf-8-sig"))
        except (FileNotFoundError, json.JSONDecodeError):
            time.sleep(0.1)
    raise RuntimeError(f"No complete diagnostic snapshot: {path}")


def intersects(mins, maxs, planes):
    # An AABB is wholly outside a plane if its most positive vertex is outside.
    return all(sum(p[i] * (maxs[i] if p[i] >= 0 else mins[i]) for i in range(3)) >= p[3]
               for p in planes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rounds", type=int, choices=range(0, 7), default=3)
    parser.add_argument("--seconds", type=int, choices=range(15, 121), default=40)
    parser.add_argument("--label", default="public-player-callback")
    args = parser.parse_args()
    if not args.label or any(c not in "abcdefghijklmnopqrstuvwxyz0123456789-_" for c in args.label):
        raise ValueError("Use a short lowercase evidence label")
    renderer = module("renderer_observer", ROOT / "tools/Exercise-RendererPerformance.py")
    rcon = module("studio_rcon", ROOT / "tools/GoldSrc-Command.py")
    original = renderer.read_status()
    renderer.command("render_options 1 1")
    seen, records, faults, stale = {}, [], [], 0
    initial = read_json(SERVER)
    start = time.monotonic()
    try:
        for cycle in range(max(1, args.rounds)):
            if args.rounds:
                rcon.command("sv_restart 1")
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline:
                rcon.command("gc_zp_status")
                server = read_json(SERVER)
                snapshot = read_json(DIAGNOSTICS)
                players = {p["slot"]: p for p in server["players"]}
                fresh = []
                for p in snapshot["players"]:
                    box = p.get("bounds")
                    if not box or "serial" not in box:
                        continue
                    identity = (p["entity"], box["serial"])
                    if seen.get(p["entity"]) == identity or abs(snapshot["time"] - box["time"]) > 2.5:
                        stale += 1
                        continue
                    seen[p["entity"]] = identity
                    actor = players.get(p["entity"], {})
                    center = [(a + b) / 2 for a, b in zip(box["mins"], box["maxs"])]
                    offset = math.dist(center, box["origin"])
                    hull_min = [box["origin"][i] - (36 if i == 2 else 16) for i in range(3)]
                    hull_max = [box["origin"][i] + (36 if i == 2 else 16) for i in range(3)]
                    observation = {"entity": p["entity"], "alive": actor.get("alive"),
                                   "zombie": actor.get("zombie"), "offset": offset,
                                   "humanHullInView": intersects(hull_min, hull_max, box["planes"]),
                                   "boxInView": intersects(box["mins"], box["maxs"], box["planes"]),
                                   "diagnostic": p}
                    fresh.append(observation)
                    # Death ragdolls may deliberately leave the server origin.
                    # Keep them in the evidence, but only assert on living players.
                    if actor.get("alive") and offset > 160:
                        faults.append({"cycle": cycle + 1, "time": snapshot["time"], **observation})
                records.append({"cycle": cycle + 1, "elapsed": time.monotonic() - start,
                                "clientTime": snapshot["time"], "deferred": snapshot["deferred"],
                                "server": server, "fresh": fresh})
                time.sleep(1)
            living = [p for r in records if r["cycle"] == cycle + 1 for p in r["fresh"] if p["alive"]]
            print(json.dumps({"cycle": cycle + 1, "freshLivingSamples": len(living),
                              "maxOffset": max((p["offset"] for p in living), default=None),
                              "faultsSoFar": len(faults)}), flush=True)
    finally:
        renderer.command(f"render_options {int(original.get('rendererDeferred', 1))} "
                         f"{int(original.get('rendererDiagnostics', 0))}")
    living = [p for r in records for p in r["fresh"] if p["alive"]]
    checks = {
        "freshMainViewLivingBoundsObserved": len(living) >= 40,
        "humanAndZombieModelsObserved": {p["zombie"] for p in living} >= {0, 1},
        "noDisplacedLivingPhysicsBounds": not faults,
        "deferredEnabledThroughout": all(r["deferred"] == 1 for r in records),
        "actualTwentyFourBots": all(sum(bool(p["bot"]) for p in r["server"]["players"]) == 24 for r in records),
        "serverCureLifecycleAdvanced": records[-1]["server"]["cures"] > initial["cures"],
        "bodyMeshesObserved": any(p["diagnostic"]["meshes"] > 0 for p in living),
    }
    dlls = {name: hashlib.sha256((CLIENT / "Half-Life/cstrike/metahook/plugins" / name).read_bytes()).hexdigest()
            for name in ("BulletPhysics_AVX2.dll", "Renderer_AVX2.dll")}
    report = {"label": args.label, "checks": checks, "passed": all(checks.values()),
              "roundRestarts": args.rounds, "staleSnapshotsExcluded": stale, "dlls": dlls,
              "faults": faults, "records": records,
              "scope": "Main-view numerical bounds and autonomous ZP lifecycle; no input/visual acceptance implied."}
    destination = ROOT / "analysis/goldcraft-tests" / f"studio-lifecycle-{time.time_ns()}.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"checks": checks, "faults": len(faults), "report": str(destination)}), flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
