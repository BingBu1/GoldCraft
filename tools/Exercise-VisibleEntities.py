"""Verify the real B client's 4096 visible queue and main-view sprite draws.

Uses the accepted human observer demo and bounded native test commands; no
desktop input, server entities, generated assets or server restart. Post-GL
draw counters and saved framebuffers are complementary evidence. This does
not establish 4096 network edicts or arbitrary models' rendering performance.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import time

ROOT = Path(__file__).resolve().parent.parent
CLIENT = ROOT / "sandbox/cs-client-b"
EVIDENCE = ROOT / "analysis/visible-entities"


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def main():
    lifecycle = module("visible_lifecycle", "Exercise-PrecacheTransitions.py")
    renderer = module("visible_renderer", "Exercise-RendererPerformance.py")
    reference = lifecycle.read(ROOT / "analysis/native-regressions/native-reference-recording.json")
    demo = CLIENT / "Half-Life/cstrike/goldcraft_native_reference.dem"
    if not reference.get("accepted") or hashlib.sha256(demo.read_bytes()).hexdigest() != reference["demoSha256"]:
        raise ValueError("A validated human-selected observer demo is required")
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    run = time.time_ns()
    before = lifecycle.status()
    process = lifecycle.read(CLIENT / "process-CsClient.json")
    cluster = lifecycle.read(ROOT / "sandbox/cluster.json")
    server_process = lifecycle.read(ROOT / "sandbox/cs-server/process-CsServer.json")
    report = {"scope": __doc__, "pid": process["pid"], "serverProcess": server_process,
              "reference": reference, "before": before, "rounds": [], "passed": False}
    report["hashes"] = {name: hashlib.sha256((CLIENT / "Half-Life/cstrike/metahook/plugins" / name).read_bytes()).hexdigest()
                        for name in ("GoldCraft.dll", "Renderer_AVX2.dll", "BulletPhysics_AVX2.dll")}

    def wait_for(predicate, seconds=15):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            state = lifecycle.status()
            if predicate(state):
                return state
            time.sleep(.1)
        raise TimeoutError("Live visible-entity condition did not become true")

    def capacity_ok(state):
        v = state["visibleEntities"]
        return (v["enabled"] and v["capacity"] == 4096 and v["transparentCapacity"] >= 4096
                and v["rendererBound"] and v["bulletBound"] and not v["error"])

    try:
        report["populationBefore"] = renderer.server_population()
        if not capacity_ok(before):
            raise AssertionError("Visible table, consumers or transparent capacity are unavailable")
        renderer.command("render_demo_play")
        wait_for(lambda s: s.get("demoPlayback") and s["viewReady"])
        time.sleep(6)
        renderer.command("render_options 1 0")
        renderer.command("render_shadow_cull 1")
        renderer.command("native_developer 0")
        for target in (513, 4096, 4097):
            renderer.command(f"visible_fixture {target} 4")
            start = wait_for(lambda s: s["visibleFixture"]["active"] and s["visibleFixture"]["target"] == target)
            serial = start["visibleFixture"]["serial"]
            samples = []
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                state = lifecycle.status()
                samples.append({k: state[k] for k in ("visibleEntities", "visibleFixture", "glError", "demoPlayback",
                                                      "origin", "viewAngles", "rendererDeferred", "rendererDiagnostics")})
                time.sleep(.1)
            framebuffer = CLIENT / "logs/goldcraft-frame.bmp"
            previous = framebuffer.stat().st_mtime_ns if framebuffer.exists() else 0
            renderer.command("capture")
            capture_deadline = time.monotonic() + 1
            while not framebuffer.exists() or framebuffer.stat().st_mtime_ns == previous:
                if time.monotonic() >= capture_deadline:
                    raise TimeoutError("No fresh framebuffer; start B with -Capture")
                time.sleep(.05)
            capture_state = lifecycle.status()["visibleFixture"]
            if not capture_state["active"] or capture_state["serial"] != serial:
                raise AssertionError("Fixture expired before framebuffer capture")
            time.sleep(.1)  # Let the bounded bitmap write close before copying.
            screenshot = EVIDENCE / f"live-{run}-{target}.bmp"
            shutil.copyfile(framebuffer, screenshot)
            expired = wait_for(lambda s: not s["visibleFixture"]["active"] and s["visibleEntities"]["count"] < 513)
            anchor = reference["anchor"]
            fixtures = [s["visibleFixture"] for s in samples]
            checks = {
                "capacityAndConsumers": all(capacity_ok(s) for s in samples),
                "freshActiveFixture": all(f["active"] and f["serial"] == serial and f["frames"] > 0 for f in fixtures),
                "realQueueBoundary": all(f["after"] == min(target, 4096) and f["accepted"] == min(target, 4096) - f["before"]
                                         and f["rejected"] == max(0, target - 4096) for f in fixtures),
                "allFixtureSpritesDrawnInMainView": any(f["uniqueDraws"] == f["accepted"] > 0 for f in fixtures),
                "slot513ActuallyDrawn": any(f["slot513Drawn"] for f in fixtures),
                "slot4096ActuallyDrawn": target < 4096 or any(f["slot4096Drawn"] for f in fixtures),
                "sameHumanAnchor": all(max(abs(s["origin"][i] - anchor["origin"][i]) for i in range(3)) < .1
                                       and max(abs((s["viewAngles"][i] - anchor["angles"][i] + 180) % 360 - 180) for i in range(3)) < .1
                                       and s["demoPlayback"] for s in samples),
                "deferredAndNoGlErrors": all(s["rendererDeferred"] == 1 and s["rendererDiagnostics"] == 0 and s["glError"] == 0 for s in samples),
                "automaticExpiryAndNativeFrameReset": not expired["visibleFixture"]["active"] and expired["visibleEntities"]["count"] < 513,
            }
            report["rounds"].append({"target": target, "samples": samples, "expired": expired,
                                     "screenshot": screenshot.relative_to(ROOT).as_posix(), "checks": checks})
            print(json.dumps({"target": target, "checks": checks, "fixture": fixtures[-1]}), flush=True)
            if not all(checks.values()):
                raise AssertionError(f"Visible fixture {target} failed")
        # Exercise VidInit cleanup of a still-active fixture as well as timeout.
        renderer.command("visible_fixture 4096 10")
        wait_for(lambda s: s["visibleFixture"]["active"])
        renderer.command("render_demo_end")
        joined = lifecycle.connect(cluster["csPort"], True)
        state = wait_for(lambda s: s["viewReady"] and s["precache"]["resets"] == joined["precache"]["resets"])
        report["reconnectChecks"] = {"activeFixtureCleared": not state["visibleFixture"]["active"],
                                      "newNativeFrameBelowLimit": state["visibleEntities"]["count"] < 513,
                                      "capacityStillBound": bool(capacity_ok(state)), "noGlError": state["glError"] == 0}
        if not all(report["reconnectChecks"].values()):
            raise AssertionError("Reconnect did not clear the live fixture and preserve the expansion")
        report["passed"] = True
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
    finally:
        try:
            renderer.command("visible_fixture_clear")
            if lifecycle.status().get("demoPlayback"):
                renderer.command("render_demo_end")
                lifecycle.connect(cluster["csPort"], True)
            renderer.command("observer")
            renderer.command("render_options 1 0")
            renderer.command("render_shadow_cull 1")
            renderer.command(f"native_developer {int(before['developerLevel'])}")
            if (lifecycle.status()["mouseLookState"] ^ before["mouseLookState"]) & 1:
                renderer.command(f"native_mlook {int(bool(before['mouseLookState'] & 1))}")
            report["after"] = lifecycle.status()
            report["populationAfter"] = renderer.server_population()
            report["restorationChecks"] = {
                "sameBProcess": lifecycle.read(CLIENT / "process-CsClient.json") == process,
                "sameServerProcess": lifecycle.read(ROOT / "sandbox/cs-server/process-CsServer.json") == server_process,
                "oneHumanAnd24Bots": report["populationAfter"]["players"] == 25 and report["populationAfter"]["bots"] == 24,
                "noResidualFixtureOrReplay": not report["after"]["visibleFixture"]["active"] and not report["after"]["demoPlayback"],
            }
            report["passed"] = report["passed"] and all(report["restorationChecks"].values())
        except Exception as error:
            report["passed"] = False
            report["restorationError"] = f"{type(error).__name__}: {error}"
        destination = EVIDENCE / f"live-{run}.json"
        destination.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps({"passed": report["passed"], "report": destination.relative_to(ROOT).as_posix(),
                          "error": report.get("error"), "restorationError": report.get("restorationError")}), flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
