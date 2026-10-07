"""Compare real CS/MC player bodies and verify native disconnect removes the MC ghost."""
import importlib.util
import json
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("renderer", ROOT / "tools/Exercise-Renderer.py")
renderer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(renderer)


def main():
    a, b = renderer.Sandbox(), renderer.Sandbox("cs-client-b")
    prefix = ROOT / "analysis/goldcraft-tests" / f"avatars-{int(time.time())}"
    report = {"scope": __doc__, "commands": a.commands, "checks": {}, "captures": {}}
    originals = {}
    disconnected = False

    def sample():
        return {"A": a.status(), "B": b.status(), "server": renderer.read_json(renderer.SERVER_STATUS)}

    def wait_for(predicate, seconds=20):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            state = sample()
            if predicate(state):
                return state
            time.sleep(.1)
        raise TimeoutError("Avatar runtime condition timed out")

    try:
        report["before"] = wait_for(lambda s: s["A"]["minecraftControl"] and s["B"]["minecraftControl"]
                                    and s["A"]["avatarCount"] == s["B"]["avatarCount"] == 1)
        for key, sandbox, position, yaw in (("A", a, "-11.5 59.001 70.125", 0), ("B", b, "-7.5 59.001 70.125", 180)):
            state = sandbox.status()
            originals[key] = {"feet": state["minecraftFeet"], "angles": state["viewAngles"]}
            a.minecraft(f"execute as {sandbox.player} in {renderer.DIMENSION} run tp @s {position}")
            sandbox.host(f"view {yaw} 0")
        time.sleep(.9)
        for enabled in (0, 1):
            for sandbox in (a, b):
                sandbox.host(f"avatars {enabled}")
            time.sleep(.7)
            report["captures"][str(enabled)] = {
                key: sandbox.capture(prefix.with_name(prefix.name + f"-{enabled}-{key}.bmp"))
                for key, sandbox in (("A", a), ("B", b))
            }
        for key in ("A", "B"):
            off, on = (report["captures"][str(enabled)][key] for enabled in (0, 1))
            report["checks"][key + "_matched_view"] = off["stable"] and on["stable"] and renderer.same_view(off["after"], on["before"])
            report["checks"][key + "_replacement_active"] = on["after"]["suppressedModels"] > off["after"]["suppressedModels"] and on["after"]["avatarCount"] == 1
            report["checks"][key + "_gl_error_zero"] = on["after"]["glError"] == off["after"]["glError"] == 0

        old = b.status()
        disconnected = True
        b.host("disconnect")
        gone = wait_for(lambda s: s["A"]["avatarCount"] == 0 and not any(p["slot"] == old["playerSlot"] for p in s["server"]["actors"]))
        spectator = a.minecraft("data get entity GoldCraft_B playerGameType")
        report["disconnected"] = {"state": gone, "minecraftGameMode": spectator}
        report["captures"]["disconnected"] = a.capture(prefix.with_name(prefix.name + "-disconnected-A.bmp"))
        report["checks"]["no_live_ghost_after_CS_disconnect"] = spectator.endswith("3") and gone["A"]["avatarCount"] == 0
        b.host("reconnect")
        rebound = wait_for(lambda s: s["A"]["avatarCount"] == s["B"]["avatarCount"] == 1 and s["B"]["minecraftControl"]
                           and s["B"]["playerSerial"] > old["playerSerial"], 30)
        disconnected = False
        report["reconnected"] = rebound
        report["checks"]["new_connection_replaces_new_avatar"] = rebound["A"]["minecraftControl"] and rebound["B"]["glError"] == 0
        report["outcome"] = "passed; matched images require visual inspection" if all(report["checks"].values()) else "failed or inconclusive"
    except Exception as error:
        report["outcome"] = "incomplete"
        report["error"] = str(error)
    finally:
        if disconnected:
            try:
                b.host("reconnect")
            except Exception as error:
                report["cleanupReconnectError"] = str(error)
        for key, sandbox in (("A", a), ("B", b)):
            try:
                sandbox.host("avatars 1")
                if key in originals:
                    prior = originals[key]
                    a.minecraft(f"execute as {sandbox.player} in {renderer.DIMENSION} run tp @s {' '.join(map(str, prior['feet']))}")
                    sandbox.host(f"view {prior['angles'][1]} {prior['angles'][0]}")
            except Exception as error:
                report[key + "_cleanupError"] = str(error)
            sandbox.connection.close()
        prefix.with_suffix(".json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"report": str(prefix.with_suffix('.json')), "outcome": report["outcome"], "checks": report["checks"], "error": report.get("error")}, indent=2))


if __name__ == "__main__":
    main()
