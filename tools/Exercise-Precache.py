"""Send the opt-in server media fixture and observe the real B client; no UI input."""
import importlib.util
import argparse
import json
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("goldsrc_command", ROOT / "tools/GoldSrc-Command.py")
rcon = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rcon)
CLIENT = ROOT / "sandbox/cs-client-b/logs/goldcraft-client-status.json"
SERVER = ROOT / "sandbox/cs-server/Half-Life/cstrike/addons/amxmodx/logs/goldcraft-precache.json"


def read(path):
    # Status is written by the live process; retry a partially written snapshot.
    for _ in range(10):
        try:
            return json.loads(path.read_text(encoding="utf-8-sig"))
        except (json.JSONDecodeError, PermissionError):
            time.sleep(.02)
    raise RuntimeError(f"No complete runtime snapshot: {path.name}")


def observe(client_before, before, started):
    print(rcon.command("gc_precache_probe").strip(), flush=True)
    server = read(SERVER)
    seen_sounds, seen_entities, seen_brass, samples = set(), set(), set(), []
    deadline = time.monotonic() + 8
    last_stamp = 0
    while time.monotonic() < deadline:
        stamp = CLIENT.stat().st_mtime_ns
        if stamp > started and stamp != last_stamp:
            status = read(CLIENT)
            client = status["precache"]
            samples.append({"precache": client, "glError": status["glError"]})
            last_stamp = stamp
            seen_sounds.update((x["index"], bool(x["ambient"]), x["name"]) for x in client["soundObservations"])
            seen_entities.update((x["entity"], x["index"], x["name"]) for x in client["highEntities"])
            seen_brass.update((x["index"], x["name"]) for x in client["brassObservations"])
            if ((65535, False, "gc_probe/high_a.wav") in seen_sounds
                    and (65536, True, "gc_probe/high_b.wav") in seen_sounds
                    and {server["entities"][0], server["entities"][1]} <= {x[0] for x in seen_entities}
                    and {65535, 65536} <= {x[0] for x in seen_brass}):
                break
        time.sleep(.04)
    client = samples[-1]["precache"] if samples else client_before
    checks = {
        "liveClientProgress": bool(samples),
        "fullManifestPastLegacyLimits": client["resources"] == client["total"] > 4096 and client["cachedModels"] > 1024 and client["cachedSounds"] > 1024,
        "singleProbeNoValidationFailures": server["probes"] == before["probes"] + 1 and server["failures"] == 0,
        "amxxMessageHooksIncludingMutation": server["amxxChecks"] - before["amxxChecks"] == 6,
        "reapiHooksIncludingResizeAndReset": server["reapiChecks"] - before["reapiChecks"] == 10,
        "amxxBrassMutation": server["amxxBrassChecks"] - before["amxxBrassChecks"] == 6,
        "reapiBrassResizeAndReset": server["reapiBrassChecks"] - before["reapiBrassChecks"] == 10,
        "brass65535Received": (65535, "models/gc_probe/high.mdl") in seen_brass,
        "brass65536Received": (65536, "sprites/gc_probe/high.spr") in seen_brass,
        "brassReaderAdvanced": client["brassReads"] >= client_before["brassReads"] + 10 and client["highBrassReads"] >= client_before["highBrassReads"] + 7,
        "shadowProducerPreservesHighIndex": server["shadowExpected"] > 65535 and server["shadowMessages"] > 0 and server["shadowObserved"] == server["shadowExpected"],
        "clientShadowResolvesRealSprite": client["shadowIndex"] == server["shadowExpected"] and client["shadowName"] == "sprites/shadow_circle.spr",
        "model65535Received": (server["entities"][0], 65535, "models/gc_probe/high.mdl") in seen_entities,
        "sprite65536Received": (server["entities"][1], 65536, "sprites/gc_probe/high.spr") in seen_entities,
        "dynamicSound65535Received": (65535, False, "gc_probe/high_a.wav") in seen_sounds,
        "ambientSound65536Received": (65536, True, "gc_probe/high_b.wav") in seen_sounds,
        "tempEntityReadersAdvanced": client["highModelReads"] >= client_before["highModelReads"] + 7,
        "noClientGlError": bool(samples) and all(s["glError"] == 0 for s in samples),
    }
    return {"checks": checks, "server": server, "clientBefore": client_before,
              "samples": samples, "scope": "Sparse 65535/65536 handles and real protocol/hooks; not 65k unique resources or visual/audio perception."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wait-for-timer", action="store_true", help="Verify server cleanup without sending a clear request")
    args = parser.parse_args()
    client_before = read(CLIENT)["precache"]
    if time.time() - CLIENT.stat().st_mtime > 5:
        raise RuntimeError("B client status is stale")
    if not client_before["extended"] or client_before["receiving"] or client_before["version"] != 3 or not client_before["clientMedia"]:
        raise RuntimeError("Wait for the real B client to complete its extended manifest")
    rcon.command("gc_precache_probe_status")
    before = read(SERVER)
    if (before["highModel"], before["highSprite"], before["highSounds"]) != (65535, 65536, [65535, 65536]):
        raise RuntimeError("Start the server with the opt-in precache fixture")
    started = time.time_ns()
    report = {"checks": {}, "passed": False}
    try:
        report = observe(client_before, before, started)
        if args.wait_for_timer:
            # No cleanup request is issued during this window: Pawn owns expiry
            # even if the external observer is gone or has thrown an exception.
            deadline = time.monotonic() + 12
            while time.monotonic() < deadline:
                sample = read(SERVER)
                if sample["activeEntities"] == 0 and sample["entities"] == [0, 0]:
                    break
                time.sleep(.1)
            report["checks"]["serverTimerExpiresWithoutClear"] = sample["activeEntities"] == 0 and sample["entities"] == [0, 0]
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
    finally:
        try:
            rcon.command("gc_precache_probe_clear")
            cleaned = read(SERVER)
            report["checks"]["fixtureEntitiesRemoved"] = cleaned["entities"] == [0, 0] and cleaned["activeEntities"] == 0
            report["cleanup"] = cleaned
        except Exception as error:
            report["checks"]["fixtureEntitiesRemoved"] = False
            report["cleanupError"] = f"{type(error).__name__}: {error}"
    report["passed"] = "error" not in report and all(report["checks"].values())
    output = ROOT / "analysis/goldcraft-tests" / f"precache-boundary-{started}.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"report": output.relative_to(ROOT).as_posix(), "checks": report["checks"], "error": report.get("error")}, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
