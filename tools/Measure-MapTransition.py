"""Measure acknowledged reloads on the empty, loopback-only development server."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import statistics
import time

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("sandbox_rcon", ROOT / "tools/GoldSrc-Command.py")
RCON = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RCON)
LOGS = ROOT / "sandbox/cs-server/Half-Life/cstrike/logs"
STAGE = re.compile(r"GoldCraft mapload stage=(\w+) map=(\w+) seconds=([0-9.]+)")
MODEL_STAGE = re.compile(
    r"GoldCraft modelload phase=(\w+) stage=(\w+) calls=(\d+) seconds=([0-9.]+)")


def status():
    reply = RCON.command("status")
    players = re.search(r"players\s*:\s*(\d+) active", reply)
    current = re.search(r"map\s*:\s*(\w+) at:", reply)
    if not players or not current:
        raise RuntimeError("Unrecognized server status; refusing to reload")
    if int(players[1]):
        raise RuntimeError("A player is connected; this benchmark requires an empty server")
    return current[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", required=True)
    parser.add_argument("--samples", type=int, default=3)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_]{1,64}", args.map) or not 1 <= args.samples <= 5:
        parser.error("use a simple map name and 1–5 samples")
    output = args.output.resolve()
    if not output.is_relative_to((ROOT / "build").resolve()):
        parser.error("output must be inside the workspace build directory")
    if output.exists():
        parser.error("choose an unused result filename")
    output.parent.mkdir(parents=True, exist_ok=True)
    engine = ROOT / "sandbox/cs-server/Half-Life/swds.dll"
    result = {"map": args.map, "engineSha256": hashlib.sha256(engine.read_bytes()).hexdigest(),
              "scope": "Empty dedicated server; no client loading or gameplay assertion", "samples": []}
    if "not currently logging" in RCON.command("log"):
        raise RuntimeError("Enable sandbox logging before this benchmark")
    for index in range(args.samples):
        status()
        before = {path.name: path.stat().st_size for path in LOGS.glob("L*.log")}
        started = time.monotonic()
        request_error = None
        try:
            RCON.command("changelevel " + args.map)
        except TimeoutError as error:
            # Synchronous loading can exceed the RCON response window. A new
            # completed map log plus a subsequent status must still prove it.
            request_error = type(error).__name__
        completed = None
        while time.monotonic() - started < 60:
            for path in LOGS.glob("L*.log"):
                offset = before.get(path.name, 0)
                if path.stat().st_size <= offset:
                    continue
                with path.open("rb") as stream:
                    stream.seek(offset)
                    lines = stream.read().decode("utf-8", errors="replace")
                if f'Loading map "{args.map}"' in lines:
                    try:
                        if status() == args.map:
                            completed = (path, lines)
                            break
                    except TimeoutError:
                        pass
            if completed:
                break
            time.sleep(0.1)
        if not completed:
            raise TimeoutError("No completed map load appeared within 60 seconds")
        observed = time.monotonic() - started
        path, lines = completed
        # RCON is serviced by the same thread as map activation. A fresh Loading
        # record and its subsequent status prove return from synchronous load.
        # Reopening an already-enabled log flushes the old file's final timer.
        RCON.command("log on")
        with path.open("rb") as stream:
            stream.seek(before.get(path.name, 0))
            lines = stream.read().decode("utf-8", errors="replace")
        stages = {stage: float(seconds) for stage, name, seconds in STAGE.findall(lines) if name == args.map}
        if f'Started map "{args.map}"' not in lines:
            raise RuntimeError("Activation did not record a completed map")
        sample = {"index": index + 1, "observedSeconds": observed,
                  "stagesSeconds": stages, "log": path.name, "requestError": request_error,
                  "modelStages": [{"phase": phase, "stage": stage, "calls": int(calls),
                                   "seconds": float(seconds)}
                                  for phase, stage, calls, seconds in MODEL_STAGE.findall(lines)],
                  "diagnosticLines": [line for line in lines.splitlines() if "GoldCraft" in line],
                  "cache": RCON.command("gc_cache_stats").strip()}
        if "changelevel" in stages:
            sample["engineChangelevelSeconds"] = stages["changelevel"]
        result["samples"].append(sample)
        output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(sample), flush=True)
    result["medianObservedSeconds"] = statistics.median(s["observedSeconds"] for s in result["samples"])
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
