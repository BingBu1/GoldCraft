"""Switch the single sandbox B between stock and GoldCraft loopback servers.

Uses the opt-in plugin's bounded connection-test interface, not OS input.
Requires Legacy-ProtocolServer.ps1 -Action Run and the GoldCraft server.
"""
import importlib.util
import json
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parent.parent
LOGS = ROOT / "sandbox/cs-client-b/logs"
spec = importlib.util.spec_from_file_location("goldsrc_command", ROOT / "tools/GoldSrc-Command.py")
rcon = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rcon)


def read(path):
    for _ in range(20):
        try:
            return json.loads(path.read_text(encoding="utf-8-sig"))
        except (json.JSONDecodeError, PermissionError):
            time.sleep(.025)
    raise RuntimeError(f"Incomplete JSON snapshot: {path.name}")


def status():
    path = LOGS / "goldcraft-client-status.json"
    if time.time() - path.stat().st_mtime > 5:
        raise RuntimeError("B client stopped publishing live status")
    return read(path)


def connect(port, extended):
    before = status()
    path = LOGS / "goldcraft-test-command.txt"
    sequence = max(time.time_ns() // 1_000_000, before["testCommand"], int(path.read_text().split()[0])) + 1
    temporary = path.with_suffix(".tmp")
    temporary.write_text(f"{sequence} loopback_connect {port}\n", encoding="ascii")
    temporary.replace(path)
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        sample = status()
        precache = sample["precache"]
        if (sample["testCommand"] == sequence and precache["resets"] > before["precache"]["resets"]
                and precache["manifests"] > before["precache"]["manifests"]
                and not precache["receiving"] and bool(precache["extended"]) == extended
                and precache["shadowName"] == "sprites/shadow_circle.spr"):
            return sample
        time.sleep(.1)
    raise TimeoutError(f"Connection to loopback port {port} did not reach the expected protocol")


def main():
    legacy_config = ROOT / "sandbox/headless-combat/cluster.json"
    legacy_state = read(ROOT / "sandbox/headless-combat/legacy-protocol.json")
    cluster = read(ROOT / "sandbox/cluster.json")
    process_path = ROOT / "sandbox/cs-client-b/process-CsClient.json"
    process_before = read(process_path)
    legacy_version = rcon.command("version", config_path=legacy_config)
    rounds = []
    failure = recovery_error = None
    try:
        for name, port, extended in (
                ("stock", legacy_state["port"], False), ("goldcraft", cluster["csPort"], True),
                ("stock", legacy_state["port"], False), ("goldcraft", cluster["csPort"], True)):
            sample = connect(port, extended)
            precache = sample["precache"]
            checks = {
                "negotiatedMode": bool(precache["extended"]) == extended,
                "correctStorage": precache["storage"] == ("goldcraft" if extended else "native"),
                "completeManifest": precache["resources"] == precache["total"] > 0,
                "nativeBoundsRestored": extended or (precache["models"] == precache["sounds"] == 512),
                "realShadowSprite": precache["shadowName"] == "sprites/shadow_circle.spr",
                "stockShadowIndex": extended or 0 < precache["shadowIndex"] < 512,
                "noGlError": sample["glError"] == 0,
                "sameBProcessRecord": read(process_path) == process_before,
            }
            rounds.append({"server": name, "checks": checks, "precache": precache})
            print(json.dumps({"server": name, "checks": checks, "resources": precache["total"]}), flush=True)
            if not all(checks.values()):
                raise AssertionError(f"Protocol regression on {name}")
    except Exception as error:
        failure = f"{type(error).__name__}: {error}"
    finally:
        if failure:
            try:
                connect(cluster["csPort"], True)
            except Exception as error:
                recovery_error = f"{type(error).__name__}: {error}"
        report = {"pid": process_before["pid"], "stockEngine": legacy_state["engine"],
                  "stockVersion": legacy_version, "rounds": rounds,
                  "passed": not failure and len(rounds) == 4 and all(all(r["checks"].values()) for r in rounds),
                  "error": failure, "recoveryError": recovery_error,
                  "scope": "Real stock/GoldCraft protocol switching in one B process; not every remote server plugin."}
        output = ROOT / "analysis/goldcraft-tests" / f"precache-transitions-{time.time_ns()}.json"
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps({"report": output.relative_to(ROOT).as_posix(), "passed": report["passed"]}), flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
