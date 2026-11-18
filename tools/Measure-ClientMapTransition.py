"""Observe one real sandbox B across a local map change, without desktop input.

Fresh status, a new map epoch, a complete manifest and subsequent native HUD
callbacks are required. Status gaps are not exact rendered-frame stalls or visual QA.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import ctypes
from ctypes import wintypes
from datetime import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import time

import csol_hud_resources
from win32_processes import processes

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
    rows = processes(("hl.exe", "MetaHook.exe"))
    if any(not row.get("image") for row in rows):
        raise RuntimeError("Cannot establish all graphical client identities")
    workspace_clients = [row for row in rows if
                         Path(row["image"]).resolve().is_relative_to(ROOT / "sandbox")]
    if (len(workspace_clients) != 1 or
            Path(workspace_clients[0]["image"]).resolve() != executable or
            workspace_clients[0]["pid"] != managed["pid"]):
        raise RuntimeError("Exactly one running sandbox B client is required")
    row = workspace_clients[0]
    created = row["creationTime100ns"] / 10_000_000 - 11644473600
    if abs(created - datetime.fromisoformat(managed["startedUtc"]).timestamp()) > 1:
        raise RuntimeError("Managed client PID belongs to a different launch")
    return row


class Window:
    """Query the live owner's SDL window without activating it or sending input."""
    def __init__(self, pid):
        self.pid = pid
        self.api = ctypes.WinDLL("user32", use_last_error=True)
        self.api.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        self.api.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        self.api.IsHungAppWindow.argtypes = [wintypes.HWND]
        self.api.IsHungAppWindow.restype = wintypes.BOOL
        callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        self.api.EnumWindows.argtypes = [callback, wintypes.LPARAM]
        matches = []

        @callback
        def visit(handle, unused):
            owner = wintypes.DWORD()
            self.api.GetWindowThreadProcessId(handle, ctypes.byref(owner))
            name = ctypes.create_unicode_buffer(256)
            self.api.GetClassNameW(handle, name, len(name))
            if owner.value == pid and name.value == "SDL_app":
                matches.append(handle)
            return True

        if not self.api.EnumWindows(visit, 0) or len(matches) != 1:
            raise RuntimeError("Expected one SDL window owned by sandbox B")
        self.handle = matches[0]

    def hung(self):
        owner = wintypes.DWORD()
        if (not self.api.GetWindowThreadProcessId(self.handle, ctypes.byref(owner)) or
                owner.value != self.pid):
            raise RuntimeError("Observed SDL window was destroyed or replaced")
        return bool(self.api.IsHungAppWindow(self.handle))


def save(path, value):
    temporary = path.with_name(path.name + ".tmp")
    stream = temporary.open("x", encoding="utf-8")
    try:
        with stream:
            json.dump(value, stream, ensure_ascii=True, indent=2)
            stream.write("\n")
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


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
    if (not re.search(r"players\s*:\s*1 active", value) or
            not re.search(r'^#\s+\d+\s+"[^\n]*"[^\n]*127\.0\.0\.1:\d+\s*$', value, re.M)):
        raise RuntimeError("The local server must contain only the loopback test player")
    current = re.search(r"map\s*:\s*(\w+) at:", value)
    if not current or (expected_map and current[1] != expected_map):
        raise RuntimeError("Unexpected server map")
    return current[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=120, help="Observation limit, 10–180 seconds")
    args = parser.parse_args()
    output = args.output.resolve()
    if not re.fullmatch(r"[A-Za-z0-9_]{1,64}", args.map):
        parser.error("invalid map name")
    if (not output.is_relative_to(ROOT / "build") or output.exists() or
            any(p.is_symlink() or (p.exists() and getattr(p.stat(), "st_file_attributes", 0) & 0x400)
                for p in (args.output.absolute(), *args.output.absolute().parents))):
        parser.error("choose an unused output path under build")
    if not 10 <= args.seconds <= 180:
        parser.error("use an observation limit from 10 to 180 seconds")
    process, old_map = client_process(), server_status()
    pid = process["pid"]
    window = Window(pid)
    hud = csol_hud_resources.verify(ROOT / "sandbox/cs-server/Half-Life/cstrike")
    if not hud["success"]:
        raise RuntimeError("Server HUD download closure is incomplete")
    initial = snapshot()
    if not initial or time.time_ns() - initial[0] > 5_000_000_000 or not initial[1].get("viewReady"):
        raise RuntimeError("No fresh initial view")
    initial_stamp, before = initial
    if not before.get("mapCollisionEpoch") or before.get("precache", {}).get("receiving"):
        raise RuntimeError("Initial map is not ready")
    paths = [ROOT / "sandbox/cs-server/Half-Life/swds.dll",
             ROOT / "sandbox/cs-server/Half-Life/cstrike/dlls/mp.dll",
             CLIENT / "Half-Life/hw.dll"] + [
             CLIENT / "Half-Life/cstrike/metahook/plugins" / name for name in
             ("CSOLSupport.dll", "GoldCraft.dll", "ParallelDownload.dll", "Renderer_AVX2.dll")]
    result = {"scope": __doc__, "pid": pid, "beforeMap": old_map, "targetMap": args.map,
              "process": process, "window": int(window.handle), "hudPreflight": hud,
              "before": before,
              "modules": [{"path": str(path.relative_to(ROOT)),
                           "sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for path in paths],
              "samples": [], "passed": False, "visualAcceptance": False}
    output.parent.mkdir(parents=True, exist_ok=True)
    started, wall_started = time.monotonic(), time.time_ns()
    result["startedMonotonicSeconds"] = started
    result["startedUnixNs"] = wall_started
    last_stamp, last_report, largest_gap = initial_stamp, started, 0.0
    last_saved, last_progress = started, started
    ready_window = None
    try:
        with ThreadPoolExecutor(max_workers=1) as executor:
            request = executor.submit(RCON.command, "changelevel " + args.map)
            while time.monotonic() - started < args.seconds:
                current = snapshot()
                now = time.monotonic()
                observation = {"seconds": now - started, "hung": window.hung(),
                               "statusAgeSeconds": (time.time_ns() - last_stamp) / 1e9}
                if current and current[0] > last_stamp and current[0] > wall_started:
                    last_stamp, state = current
                    largest_gap = max(largest_gap, now - last_report)
                    last_report = now
                    cache = state.get("precache", {})
                    work = state.get("nativeWork", {})
                    sample = {"seconds": now - started, "mapEpoch": state.get("mapCollisionEpoch"),
                              "world": state.get("world"),
                              "viewReady": state.get("viewReady"), "predictionFrames": state.get("predictionFrames"),
                              "manifestResets": cache.get("resets"), "receiving": cache.get("receiving"),
                              "workWindow": work.get("window", 0),
                              "nativeHudRedraws": work.get("stages", {}).get("nativeHudRedraw", {}).get("count", 0),
                              "focused": state.get("windowFocused"),
                              "resourceSync": {key: state.get("resourceSync", {}).get(key) for key in
                                  ("stage", "phase", "token", "frames", "total", "checked", "downloaded",
                                   "cached", "current", "elapsedMs", "pumpMaxMs", "error", "sessionError")},
                              "precache": {key: cache.get(key) for key in
                                  ("resources", "total", "models", "sounds", "cachedModels", "cachedSounds",
                                   "resets", "receiving", "error")}}
                    observation.update(sample)
                    ready = (sample["mapEpoch"] and sample["mapEpoch"] != before["mapCollisionEpoch"] and
                             sample["viewReady"] and not sample["receiving"] and
                             sample["manifestResets"] > before["precache"]["resets"])
                    if ready:
                        if ready_window is not None and sample["workWindow"] > ready_window and sample["nativeHudRedraws"] > 0:
                            result["readySeconds"] = sample["seconds"]
                            result["ready"] = sample
                            result["after"] = state
                            result["samples"].append(observation)
                            break
                        if ready_window is None:
                            result["firstNewViewSeconds"] = sample["seconds"]
                            ready_window = sample["workWindow"]
                    else:
                        ready_window = None
                result["samples"].append(observation)
                if now - last_saved >= 1:
                    save(output, result)
                    last_saved = now
                if now - last_progress >= 5:
                    print(json.dumps({"seconds": observation["seconds"], "hung": observation["hung"],
                                      "statusAgeSeconds": observation["statusAgeSeconds"]}), flush=True)
                    last_progress = now
                time.sleep(.05)
            else:
                raise TimeoutError("No new map view with subsequent native HUD callbacks before observation limit")
            try:
                request.result()
                result["requestError"] = None
            except TimeoutError:
                # Map-loading can block RCON; the new epoch and status are the proof.
                result["requestError"] = "TimeoutError"
        result["serverMap"] = server_status(args.map)
        final_process = client_process()
        if (final_process["pid"], final_process["creationTime100ns"]) != (pid, process["creationTime100ns"]):
            raise RuntimeError("Client process changed during measurement")
        result["processAfter"] = final_process
        result["processCpuSeconds"] = final_process["cpuSeconds"] - process["cpuSeconds"]
        result["passed"] = True
    except Exception as error:
        result["error"] = str(error)
        raise
    finally:
        result["maxStatusGapSeconds"] = largest_gap
        result["hungObservations"] = sum(sample["hung"] for sample in result["samples"])
        save(output, result)
        print(json.dumps({key: result.get(key) for key in
                          ("passed", "readySeconds", "maxStatusGapSeconds", "error")}), flush=True)


if __name__ == "__main__":
    main()
