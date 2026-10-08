"""Exercise B's native server-entry lifecycle without keyboard or mouse input.

Requires the managed B client and ReHLDS on cs_assault. Uses the opt-in local
connection-test command, verifies fresh post-signon frames and 24 actual Bots,
and leaves B connected. UI usability and sustained rendering performance are
separate acceptance requirements.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parent.parent


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / file)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reconnects", type=int, choices=range(1, 5), default=3)
    parser.add_argument("--seconds", type=int, choices=range(5, 31), default=10)
    args = parser.parse_args()
    lifecycle = module("native_connection", "Exercise-PrecacheTransitions.py")
    renderer = module("native_join_observer", "Exercise-RendererPerformance.py")
    cluster = lifecycle.read(ROOT / "sandbox/cluster.json")
    client = ROOT / "sandbox/cs-client-b"
    process = lifecycle.read(client / "process-CsClient.json")
    dll = client / "Half-Life/cstrike/metahook/plugins/VGUI2Extension.dll"
    report = {"pid": process["pid"], "vguiSha256": hashlib.sha256(dll.read_bytes()).hexdigest(),
              "scope": "Live repeated native server entry, no simulated desktop input.", "rounds": []}
    destination = ROOT / "analysis/native-regressions" / f"native-join-{time.time_ns()}.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        for cycle in range(args.reconnects):
            joined = lifecycle.connect(cluster["csPort"], True)
            # Resource-manifest completion precedes the first rendered view.
            # Keep checking the current connection epoch; never accept a stale
            # viewReady from a previous connection or remove the live assertion.
            view_deadline = time.monotonic() + 30
            while True:
                first_view = lifecycle.status()
                if (first_view["viewReady"] and
                        first_view["precache"]["resets"] == joined["precache"]["resets"]):
                    break
                if time.monotonic() >= view_deadline:
                    raise TimeoutError("No rendered view after the current connection's signon")
                time.sleep(.1)
            samples = []
            for _ in range(args.seconds):
                state = lifecycle.status()
                samples.append({key: state[key] for key in
                                ("viewReady", "glError", "drawFrames", "rendererDeferred", "precache")})
                time.sleep(1)
            population = renderer.server_population()
            checks = {
                "sameClientProcess": lifecycle.read(client / "process-CsClient.json")["pid"] == process["pid"],
                "livePostSignonFrames": all(s["viewReady"] for s in samples),
                "completeManifest": all(s["precache"]["extended"] and not s["precache"]["receiving"] for s in samples),
                "noOpenGLError": all(s["glError"] == 0 for s in samples),
                "deferredStillEnabled": all(s["rendererDeferred"] == 1 for s in samples),
                "oneHumanAndTwentyFourBots": population["players"] == 25 and population["bots"] == 24,
            }
            record = {"cycle": cycle + 1, "manifestResets": joined["precache"]["resets"],
                      "samples": samples, "population": population, "checks": checks}
            report["rounds"].append(record)
            print(json.dumps({"cycle": cycle + 1, "checks": checks}), flush=True)
            if not all(checks.values()):
                raise AssertionError("Native join acceptance failed; inspect the recorded checks")
        report["passed"] = True
    except Exception as error:
        report["passed"] = False
        report["error"] = str(error)
        raise
    finally:
        destination.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print("Evidence: " + destination.relative_to(ROOT).as_posix(), flush=True)


if __name__ == "__main__":
    main()
