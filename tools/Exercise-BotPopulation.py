"""Verify real single-B join/leave Bot replacement on the isolated 32-slot server.

Requires --cycle-client-b: restarts only the managed B client, never a second
graphical client. The server and Bots run normally; no gameplay input is sent.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("goldsrc_command", ROOT / "tools/GoldSrc-Command.py")
rcon = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rcon)
STATUS = ROOT / "sandbox/cs-server/Half-Life/cstrike/addons/amxmodx/logs/goldcraft-zombie.json"


def client(start):
    script = ROOT / "tools" / ("Start-Sandbox.ps1" if start else "Stop-Sandbox.ps1")
    args = ["pwsh", "-NoProfile", "-File", str(script), "-Role", "CsClient", "-Instance", "cs-client-b"]
    if start:
        args.append("-Capture")
    # A GUI child can inherit the launcher's pipe handles after PowerShell exits.
    # Regular log files let wait() observe the launcher, not the game's lifetime.
    log = ROOT / "build/logs" / ("bot-population-client-start.log" if start else "bot-population-client-stop.log")
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w", encoding="utf-8") as output:
        result = subprocess.run(args, cwd=ROOT, stdin=subprocess.DEVNULL,
                                stdout=output, stderr=subprocess.STDOUT, timeout=60)
    if result.returncode:
        raise RuntimeError("Managed B launcher failed: " + log.read_text(encoding="utf-8", errors="replace"))


def population(label, humans, bots):
    start, stable, last = time.monotonic(), 0, None
    while time.monotonic() - start < 90:
        rcon.command("gc_zp_status")
        state = json.loads(STATUS.read_text(encoding="utf-8"))
        players = [{key: p[key] for key in ("slot", "userid", "bot", "team", "joining")}
                   for p in state["players"]]
        actual_bots = [p for p in players if p["bot"]]
        actual_humans = [p for p in players if not p["bot"]]
        ready = (len(actual_bots) == bots and len(actual_humans) == humans
                 and all(p["joining"] == 0 and p["team"] in (1, 2) for p in actual_bots))
        stable = stable + 1 if ready else 0
        last = {"phase": label, "elapsedSeconds": time.monotonic() - start,
                "serverTime": state["time"], "bots": actual_bots, "humans": actual_humans,
                "freeSlots": 32 - len(players)}
        if stable >= 3:
            print(f"{label}: {len(actual_humans)} human, {len(actual_bots)} Bots, {last['freeSlots']} free slots.", flush=True)
            return last
        time.sleep(1)
    raise RuntimeError("Population did not settle: " + json.dumps(last))


def bot_ids(state):
    return {p["userid"] for p in state["bots"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cycle-client-b", action="store_true", required=True)
    parser.parse_args()
    report = {"scope": "Real B connection lifecycle and native SyPB population; no gameplay input.",
              "phases": [], "checks": {}}
    restore = False
    try:
        spawn = rcon.command("gc_spawn_capacity")
        fields = {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)", spawn)}
        if fields.get("maxplayers") != 32:
            raise RuntimeError("This acceptance run requires the 32-slot sandbox server")
        report["spawns"] = fields
        client(False)
        restore = True
        empty = population("empty", 0, 25)
        report["phases"].append(empty)
        client(True)
        restore = False
        joined = population("human-joined", 1, 24)
        report["phases"].append(joined)
        client(False)
        restore = True
        left = population("human-left", 0, 25)
        report["phases"].append(left)
        client(True)
        restore = False
        returned = population("human-rejoined", 1, 24)
        report["phases"].append(returned)
        report["checks"] = {
            "nativeSpawnCounts": fields.get("T", 0) >= 16 and fields.get("CT", 0) >= 16,
            "addedSpawnsHullClear": fields.get("checked") == 12 and fields.get("blocked") == 0,
            "realHumanJoinRemovedOneBot": len(bot_ids(empty) - bot_ids(joined)) == 1,
            "realHumanLeaveRefilledOneBot": len(bot_ids(left) - bot_ids(joined)) == 1,
            "realHumanRejoinRemovedOneBot": len(bot_ids(left) - bot_ids(returned)) == 1,
            "sevenSlotsRemainAvailable": all(p["freeSlots"] == 7 for p in report["phases"]),
            "humanBRemainsConnected": len(returned["humans"]) == 1 and len(returned["bots"]) == 24,
        }
    except Exception as error:
        report["error"] = str(error)
    finally:
        if restore:
            try:
                client(True)
            except Exception as error:
                report["restoreError"] = str(error)
        report["passed"] = ("error" not in report and "restoreError" not in report
                            and bool(report["checks"]) and all(report["checks"].values()))
        path = ROOT / "analysis/goldcraft-tests" / f"bot-population-{time.time_ns()}.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps({"report": path.relative_to(ROOT).as_posix(), "passed": report["passed"],
                          "checks": report["checks"], "error": report.get("error")}), flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
