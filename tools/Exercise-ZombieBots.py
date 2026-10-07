"""Observe real SyPB movement/combat on the sandbox; never assign targets or move bots."""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("goldsrc_command", ROOT / "tools/GoldSrc-Command.py")
rcon = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rcon)
STATUS = ROOT / "sandbox/cs-server/Half-Life/cstrike/addons/amxmodx/logs/goldcraft-zombie.json"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, default=180)
    args = parser.parse_args()
    if not 10 <= args.seconds <= 600:
        raise ValueError("Observation must last 10..600 seconds")
    samples, walked, previous, paired_frames = [], {}, {}, 0
    start = time.monotonic()
    print("Observing autonomous SyPB play without gameplay commands.", flush=True)
    while time.monotonic() - start < args.seconds:
        rcon.command("gc_zp_status")
        sample = json.loads(STATUS.read_text())
        sample["elapsed"] = time.monotonic() - start
        samples.append(sample)
        for player in sample["players"]:
            if not player["bot"]:
                continue
            key = (player["userid"], player["spawns"])
            before = previous.get(key)
            if (before and before["alive"] and player["alive"] and
                    before["zombie"] == player["zombie"]):
                distance = math.dist(before["origin"][:2], player["origin"][:2])
                # Reject any obvious teleport; respawns are separated by the real
                # ReAPI spawn counter, not assumed from distance alone.
                if 1 < distance < 500 and math.hypot(*player["velocity"][:2]) > 1:
                    walked[player["userid"]] = walked.get(player["userid"], 0) + distance
            previous[key] = player
        if len(sample["players"]) >= 6 and all(
                p["team"] in (1, 2) and p["joining"] == 0 and
                p["zombie"] == p["sypbZombie"] for p in sample["players"]):
            paired_frames += 1
        time.sleep(1)
    first, last = samples[0], samples[-1]
    checks = {
        "matchingApiAndZombieMode": all(s["api"] == 1.5 and s["mode"] == 2 for s in samples),
        "sixBotsJoinedWithMatchingZombieState": paired_frames >= 5,
        "atLeastFourBotsWalkAutonomously": sum(v > 256 for v in walked.values()) >= 4,
        "naturalBotInfection": last["botInfections"] > first["botInfections"],
        "actualBotDamage": last["botDamageEvents"] > first["botDamageEvents"],
        "zombieAndArmedHumanObserved": any(
            any(p["zombie"] and p["alive"] for p in s["players"]) and
            any(not p["zombie"] and p["alive"] and p["weapon"] not in (0, 29) for p in s["players"])
            for s in samples),
    }
    report = {"checks": checks, "passed": all(checks.values()), "walkedUnits": walked,
              "samples": samples, "scope": "Autonomous native SyPB/ZP only; paired Minecraft interaction is separate."}
    path = ROOT / "analysis/goldcraft-tests" / f"zombie-bots-{time.time_ns()}.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"report": str(path.relative_to(ROOT)), "checks": checks, "walkedUnits": walked}), flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
