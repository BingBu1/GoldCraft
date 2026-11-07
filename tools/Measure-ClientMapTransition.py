"""Observe one real sandbox B across a local map change, without desktop input.

Fresh status, a new map epoch, a complete manifest and subsequent native HUD
callbacks are required. Status gaps are not exact rendered-frame stalls or visual QA.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
CLIENT = ROOT / "sandbox/cs-client-b"
STATUS = CLIENT / "logs/goldcraft-client-status.json"
SPEC = importlib.util.spec_from_file_location("sandbox_rcon", ROOT / "tools/GoldSrc-Command.py")
RCON = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RCON)


def client_process():
    managed = json.loads((CLIENT / "process-CsClient.json").read_text(encoding="utf-8-sig"))
    executable = Path(managed["executable"]).resolve()
    if not executable.is_relative_to(CLIENT / "Half-Life"):
        raise RuntimeError("Managed client path is outside sandbox B")
    query = ("Get-CimInstance Win32_Process -Filter \"Name='hl.exe' OR Name='MetaHook.exe'\" | "
             "Select-Object ProcessId,ExecutablePath | ConvertTo-Json -Compress")
    value = subprocess.check_output(["pwsh", "-NoProfile", "-Command", query], encoding="utf-8-sig")
    rows = json.loads(value) if value.strip() else []
    if isinstance(rows, dict):
        rows = [rows]
    workspace_clients = [row for row in rows if row["ExecutablePath"] and
                         Path(row["ExecutablePath"]).resolve().is_relative_to(ROOT / "sandbox")]
    if (len(workspace_clients) != 1 or
            Path(workspace_clients[0]["ExecutablePath"]).resolve() != executable or
            workspace_clients[0]["ProcessId"] != managed["pid"]):
        raise RuntimeError("Exactly one running sandbox B client is required")
    return workspace_clients[0]["ProcessId"]


def snapshot():
    try:
        stamp = STATUS.stat().st_mtime_ns
        value = json.loads(STATUS.read_text(encoding="utf-8-sig"))
        if STATUS.stat().st_mtime_ns != stamp:
            return None
        return stamp, value
    except (OSError, ValueError):
        return None


def server_status(expected_map=None):
    value = RCON.command("status")
    if not re.search(r"players\s*:\s*1 active", value) or "GoldCraft_B" not in value:
        raise RuntimeError("The local server must contain only GoldCraft_B")
    current = re.search(r"map\s*:\s*(\w+) at:", value)
    if not current or (expected_map and current[1] != expected_map):
        raise RuntimeError("Unexpected server map")
    return current[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not re.fullmatch(r"[A-Za-z0-9_]{1,64}", args.map):
        parser.error("invalid map name")
    if not output.is_relative_to(ROOT / "build") or output.exists():
        parser.error("choose an unused output path under build")
    pid, old_map = client_process(), server_status()
    initial = snapshot()
    if not initial or time.time_ns() - initial[0] > 5_000_000_000 or not initial[1].get("viewReady"):
        raise RuntimeError("No fresh initial view")
    initial_stamp, before = initial
    if not before.get("mapCollisionEpoch") or before.get("precache", {}).get("receiving"):
        raise RuntimeError("Initial map is not ready")
    paths = [ROOT / "sandbox/cs-server/Half-Life/swds.dll",
             ROOT / "sandbox/cs-server/Half-Life/cstrike/dlls/mp.dll",
             CLIENT / "Half-Life/cstrike/metahook/plugins/CSOLSupport.dll"]
    result = {"scope": __doc__, "pid": pid, "beforeMap": old_map, "targetMap": args.map,
              "modules": [{"path": str(path.relative_to(ROOT)),
                           "sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for path in paths],
              "samples": [], "passed": False, "visualAcceptance": False}
    output.parent.mkdir(parents=True, exist_ok=True)
    started, wall_started = time.monotonic(), time.time_ns()
    last_stamp, last_report, largest_gap = initial_stamp, started, 0.0
    ready_window = None
    try:
        with ThreadPoolExecutor(max_workers=1) as executor:
            request = executor.submit(RCON.command, "changelevel " + args.map)
            while time.monotonic() - started < 60:
                current = snapshot()
                now = time.monotonic()
                if current and current[0] > last_stamp and current[0] > wall_started:
                    last_stamp, state = current
                    largest_gap = max(largest_gap, now - last_report)
                    last_report = now
                    cache = state.get("precache", {})
                    work = state.get("nativeWork", {})
                    sample = {"seconds": now - started, "mapEpoch": state.get("mapCollisionEpoch"),
                              "viewReady": state.get("viewReady"), "predictionFrames": state.get("predictionFrames"),
                              "manifestResets": cache.get("resets"), "receiving": cache.get("receiving"),
                              "workWindow": work.get("window", 0),
                              "nativeHudRedraws": work.get("stages", {}).get("nativeHudRedraw", {}).get("count", 0),
                              "focused": state.get("windowFocused")}
                    result["samples"].append(sample)
                    ready = (sample["mapEpoch"] and sample["mapEpoch"] != before["mapCollisionEpoch"] and
                             sample["viewReady"] and not sample["receiving"] and
                             sample["manifestResets"] > before["precache"]["resets"])
                    if ready:
                        if ready_window is not None and sample["workWindow"] > ready_window and sample["nativeHudRedraws"] > 0:
                            result["readySeconds"] = sample["seconds"]
                            result["ready"] = sample
                            break
                        if ready_window is None:
                            result["firstNewViewSeconds"] = sample["seconds"]
                            ready_window = sample["workWindow"]
                    else:
                        ready_window = None
                time.sleep(.05)
            else:
                raise TimeoutError("No new map view with subsequent native HUD callbacks within 60 seconds")
            try:
                request.result()
                result["requestError"] = None
            except TimeoutError:
                # Map-loading can block RCON; the new epoch and status are the proof.
                result["requestError"] = "TimeoutError"
        result["serverMap"] = server_status(args.map)
        if client_process() != pid:
            raise RuntimeError("Client process changed during measurement")
        result["passed"] = True
    except Exception as error:
        result["error"] = str(error)
        raise
    finally:
        result["maxStatusGapSeconds"] = largest_gap
        output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({key: result.get(key) for key in
                          ("passed", "readySeconds", "maxStatusGapSeconds", "error")}), flush=True)


if __name__ == "__main__":
    main()
