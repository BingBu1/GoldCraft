"""Compare native Renderer builds using one sandbox B demo and a shared probe.

The probe lives in GoldCraft, so the unmodified reference DLL needs no profiler
patch. CPU figures cover the main thread between HUD callbacks, not just the
renderer. Background runs cannot establish foreground FPS. No desktop input is
generated. The fixed-name local demo and reports stay outside publication.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import time

ROOT = Path(__file__).resolve().parent.parent
CLIENT = ROOT / "sandbox/cs-client-b"
GAME = CLIENT / "Half-Life/cstrike"
DEMO = GAME / "goldcraft_native_reference.dem"
PROFILE = CLIENT / "logs/native-frame-profile.json"
EVIDENCE = ROOT / "analysis/native-regressions"


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(frames):
    if len(frames) < 20 or any(f["cpu"] < 0 for f in frames):
        raise ValueError("Insufficient frames or unavailable main-thread CPU clock")
    intervals = [b["time"] - a["time"] for a, b in zip(frames, frames[1:])]
    duration = frames[-1]["time"] - frames[0]["time"]
    cpu = frames[-1]["cpu"] - frames[0]["cpu"]
    if duration <= 0 or cpu < 0:
        raise ValueError("Invalid monotonic profile clocks")
    return {"frames": len(frames), "focusedFrames": sum(f["focused"] for f in frames),
            "allPlayback": all(f["playback"] for f in frames),
            "foregroundFpsEligible": all(f["focused"] for f in frames),
            "observedFps": (len(frames) - 1) * 1000 / duration,
            "mainThreadCpuMsPerFrame": cpu / (len(frames) - 1),
            "mainThreadCpuMs": cpu, "wallMs": duration,
            "intervalP95Ms": sorted(intervals)[int(len(intervals) * .95)],
            "clientTimeRange": [frames[0]["clientTime"], frames[-1]["clientTime"]],
            "originRange": [max(f["origin"][i] for f in frames) - min(f["origin"][i] for f in frames)
                            for i in range(3)],
            "anglesRange": [max(f["angles"][i] for f in frames) - min(f["angles"][i] for f in frames)
                            for i in range(3)]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("record", "sample"))
    parser.add_argument("--seconds", type=int, choices=range(5, 31), default=20)
    parser.add_argument("--label", default="native-reference")
    parser.add_argument("--culling", type=int, choices=(0, 1))
    parser.add_argument("--developer", type=int, choices=(0, 1))
    parser.add_argument("--renderer-profile", action="store_true",
                        help="Also capture the modified Renderer's stage/cache counters during the replay")
    parser.add_argument("--anchor-current-view", action="store_true",
                        help="Record only after the human has selected a stationary spectator view")
    args = parser.parse_args()
    if not args.label or any(c not in "abcdefghijklmnopqrstuvwxyz0123456789-_" for c in args.label):
        parser.error("Use a short lowercase evidence label")
    renderer = module("reference_renderer", "Exercise-RendererPerformance.py")
    lifecycle = module("reference_lifecycle", "Exercise-PrecacheTransitions.py")
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    metadata = EVIDENCE / "native-reference-recording.json"
    if args.action == "record":
        if not args.anchor_current_view:
            parser.error("Obtain the human's chosen spectator view, then use --anchor-current-view")
        if DEMO.exists() and (not metadata.exists() or
                             json.loads(metadata.read_text())["demoSha256"] != sha(DEMO)):
            raise ValueError("Refusing to overwrite an unrecognized native demo")
        before = renderer.server_population()
        if before["bots"] != 24 or before["players"] != 25:
            raise ValueError("Record the required one-human/24-Bot scene")
        server_path = ROOT / "sandbox/cs-server/Half-Life/cstrike/addons/amxmodx/logs/goldcraft-zombie.json"
        server = lifecycle.read(server_path)
        humans = [p for p in server["players"] if not p["bot"]]
        start = lifecycle.status()
        if len(humans) != 1 or humans[0]["team"] != 3 or not start["viewReady"]:
            raise ValueError("A live human-selected spectator view is required; no position was changed")
        anchor = {"origin": start["origin"], "angles": start["viewAngles"],
                  "spectatorSlot": humans[0]["slot"], "samples": []}
        renderer.command("capture")
        time.sleep(.25)
        shutil.copyfile(CLIENT / "logs/goldcraft-frame.bmp", EVIDENCE / "native-reference-anchor.bmp")
        renderer.command("render_demo_record")
        try:
            deadline = time.monotonic() + 40
            while time.monotonic() < deadline:
                state = lifecycle.status()
                anchor["samples"].append({k: state.get(k) for k in
                                          ("origin", "viewAngles", "viewReady", "windowFocused")})
                time.sleep(.25)
        finally:
            renderer.command("render_demo_stop")
        if not DEMO.is_file() or DEMO.stat().st_size < 4096:
            raise ValueError("The native recorder did not produce a usable demo")
        anchor["maxPositionDrift"] = max(max(abs(x-y) for x,y in zip(s["origin"], anchor["origin"]))
                                         for s in anchor["samples"])
        anchor["maxAngleDrift"] = max(max(abs((x-y+180) % 360-180) for x,y in zip(s["viewAngles"], anchor["angles"]))
                                      for s in anchor["samples"])
        after = renderer.server_population()
        humans_after = [p for p in lifecycle.read(server_path)["players"] if not p["bot"]]
        accepted = (anchor["maxPositionDrift"] < .1 and anchor["maxAngleDrift"] < .1 and
                    all(s["viewReady"] for s in anchor["samples"]) and len(humans_after) == 1 and
                    humans_after[0]["team"] == 3 and humans_after[0]["slot"] == anchor["spectatorSlot"])
        (EVIDENCE / "native-reference-anchor.json").write_text(json.dumps(anchor, indent=2), encoding="utf-8")
        record = {"accepted": accepted, "demoSha256": sha(DEMO), "bytes": DEMO.stat().st_size,
                  "map": "cs_assault", "populationBefore": before,
                  "populationAfter": after, "anchor": {k:v for k,v in anchor.items() if k != "samples"}}
        metadata.write_text(json.dumps(record, indent=2), encoding="utf-8")
        print(json.dumps(record), flush=True)
        if not accepted:
            raise AssertionError("Camera or spectator state changed; this recording is not a comparison baseline")
        return
    expected = json.loads(metadata.read_text(encoding="utf-8"))
    if not expected.get("accepted"):
        raise ValueError("Record an accepted human-selected spectator anchor first")
    if sha(DEMO) != expected["demoSha256"]:
        raise ValueError("Reference demo no longer matches its recording")
    report = {"scope": __doc__, "demo": expected, "label": args.label,
              "rendererSha256": sha(GAME / "metahook/plugins/Renderer_AVX2.dll"),
              "probeSha256": sha(GAME / "metahook/plugins/GoldCraft.dll"),
              "requestedCulling": args.culling, "requestedDeveloper": args.developer, "passed": False}
    before = lifecycle.status()
    mouse_look = before.get("mouseLookState", -1)
    developer = before.get("developerLevel")
    report["inputBefore"] = {k: before.get(k) for k in ("mouseLookState", "developerLevel")}
    renderer_profile = GAME / "renderer/frame-profile.json"
    try:
        if args.developer is not None:
            if developer is None:
                raise ValueError("This probe build cannot verify/restore developer mode")
            renderer.command(f"native_developer {args.developer}")
        renderer.command("render_options 1 0")
        if args.culling is not None:
            renderer.command(f"render_shadow_cull {args.culling}")
        renderer.command("render_demo_play")
        # Do not leave a pending probe that would silently finish on the live
        # server after a failed playback. Require real rendered replay frames.
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            state = lifecycle.status()
            if state.get("demoPlayback") and state["viewReady"]:
                break
            time.sleep(.1)
        else:
            report["playbackFailureStatus"] = state
            raise TimeoutError("Native demo did not reach rendered playback")
        time.sleep(6)  # Same initial settling interval; later stalls remain in the sample.
        # Signon/demo commands can reset client cvars. Establish and verify the
        # requested options on the rendered replay, not just before loading it.
        report["statusAfterPlaybackLoad"] = {k: lifecycle.status().get(k) for k in
                                             ("developerLevel", "rendererDeferred", "rendererShadowCull")}
        renderer.command("render_options 1 0")
        if args.culling is not None:
            renderer.command(f"render_shadow_cull {args.culling}")
        if args.developer is not None:
            renderer.command(f"native_developer {args.developer}")
        state = lifecycle.status()
        report["statusBeforeProbe"] = {k: state.get(k) for k in
                                      ("developerLevel", "rendererDeferred", "rendererShadowCull", "rendererDiagnostics")}
        def options_applied(snapshot):
            return (snapshot.get("rendererDeferred") == 1 and
                    snapshot.get("rendererDiagnostics") == 0 and
                    (args.culling is None or snapshot.get("rendererShadowCull") == args.culling) and
                    (args.developer is None or snapshot.get("developerLevel") == args.developer))
        if not options_applied(state):
            raise AssertionError("Replay did not adopt the requested measured options")
        renderer_previous = renderer_profile.stat().st_mtime_ns if renderer_profile.exists() else 0
        if args.renderer_profile:
            renderer.command(f"render_profile {args.seconds} 0")
        previous = PROFILE.stat().st_mtime_ns if PROFILE.exists() else 0
        renderer.command(f"native_profile {args.seconds}")
        deadline = time.monotonic() + args.seconds + 15
        while time.monotonic() < deadline:
            if PROFILE.exists() and PROFILE.stat().st_mtime_ns != previous:
                try:
                    data = json.loads(PROFILE.read_text(encoding="utf-8-sig"))
                except json.JSONDecodeError:
                    time.sleep(.1)
                    continue
                break
            time.sleep(.2)
        else:
            raise TimeoutError("The shared native frame probe did not finish")
        report["profile"] = data
        report["summary"] = summarize(data["frames"])
        state = lifecycle.status()
        report["statusAfter"] = {k: state.get(k) for k in
                                  ("viewReady", "glError", "windowFocused", "rendererDeferred", "mouseLookState",
                                   "developerLevel", "rendererShadowCull", "rendererDiagnostics")}
        anchor = expected["anchor"]
        report["anchorPositionError"] = max(abs(f["origin"][i]-anchor["origin"][i])
                                            for f in data["frames"] for i in range(3))
        report["anchorAngleError"] = max(abs((f["angles"][i]-anchor["angles"][i]+180) % 360-180)
                                         for f in data["frames"] for i in range(3))
        if args.renderer_profile:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if renderer_profile.exists() and renderer_profile.stat().st_mtime_ns != renderer_previous:
                    detail = lifecycle.read(renderer_profile)
                    break
                time.sleep(.1)
            else:
                raise TimeoutError("Renderer stage probe did not complete during the replay")
            report["rendererProfile"] = detail
            report["rendererSummary"] = renderer.summarize(detail["frames"])
            report["rendererAnchorPositionError"] = max(abs(f["origin"][i]-anchor["origin"][i])
                                                        for f in detail["frames"] for i in range(3))
            report["rendererAnchorAngleError"] = max(abs((f["angles"][i]-anchor["angles"][i]+180) % 360-180)
                                                     for f in detail["frames"] for i in range(3))
            if report["rendererAnchorPositionError"] >= .1 or report["rendererAnchorAngleError"] >= .1:
                raise AssertionError("Renderer stage sample left the human-selected reference view")
        report["passed"] = (report["summary"]["allPlayback"] and report["summary"]["foregroundFpsEligible"] and
                            report["anchorPositionError"] < .1 and report["anchorAngleError"] < .1 and
                            report["statusAfter"]["glError"] == 0 and
                            options_applied(state))
        print(json.dumps({"label": args.label, "summary": report["summary"], "passed": report["passed"]}), flush=True)
        if args.renderer_profile:
            print(json.dumps({"rendererCounts": report["rendererSummary"]["meanCounts"],
                              "shadow": report["rendererSummary"]["shadow"]}), flush=True)
    finally:
        try:
            renderer.command("native_profile_stop")
            renderer.command("render_demo_end")
            cluster = lifecycle.read(ROOT / "sandbox/cluster.json")
            lifecycle.connect(cluster["csPort"], True)
            renderer.command("observer")
            if mouse_look >= 0:
                restored = lifecycle.status()["mouseLookState"]
                report["mouseLookChangedDuringReplay"] = bool((restored ^ mouse_look) & 1)
                if report["mouseLookChangedDuringReplay"]:
                    renderer.command(f"native_mlook {int(bool(mouse_look & 1))}")
            if args.culling is not None:
                renderer.command("render_shadow_cull 1")
            if args.developer is not None and developer in (0, 1):
                renderer.command(f"native_developer {int(developer)}")
        finally:
            target = EVIDENCE / f"{args.label}-{time.time_ns()}.json"
            target.write_text(json.dumps(report, separators=(",", ":")), encoding="utf-8")
            print("Evidence: " + target.relative_to(ROOT).as_posix(), flush=True)
    if not report["passed"]:
        raise AssertionError("Playback, anchor, rendering or foreground acceptance failed; inspect the recorded fields")


if __name__ == "__main__":
    main()
