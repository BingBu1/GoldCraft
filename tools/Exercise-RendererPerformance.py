"""Measure the live B renderer without sending mouse, keyboard or gameplay input.

The Renderer stores bounded CPU timings in memory and writes once per sample.
Nested scene/stage timings are inclusive. Focus/camera changes invalidate an FPS
comparison; CPU stage samples remain useful for identifying expensive work.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import statistics
import time

ROOT = Path(__file__).resolve().parent.parent
LOGS = ROOT / "sandbox/cs-client-b/logs"
STATUS = LOGS / "goldcraft-client-status.json"
PROFILE = ROOT / "sandbox/cs-client-b/Half-Life/cstrike/renderer/frame-profile.json"
STAGES = ("interval", "render", "setup", "shadow", "water", "scene",
          "ambientOcclusion", "lights", "composite", "post", "endFrame", "present",
          "pointShadow", "spotShadow", "directionalShadow", "studioShadow", "studioMain",
          "studioBoneSetup", "studioBoneSave", "studioBoneMerge", "studioMesh", "studioSubmit")


def server_population():
    spec = importlib.util.spec_from_file_location("goldsrc_command", ROOT / "tools/GoldSrc-Command.py")
    rcon = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(rcon)
    rcon.command("gc_zp_status")
    path = ROOT / "sandbox/cs-server/Half-Life/cstrike/addons/amxmodx/logs/goldcraft-zombie.json"
    state = json.loads(path.read_text(encoding="utf-8-sig"))
    return {"serverTime": state["time"], "players": len(state["players"]),
            "bots": sum(bool(p["bot"]) for p in state["players"]),
            "aliveBots": sum(bool(p["bot"] and p["alive"]) for p in state["players"])}


def read_status():
    try:
        return json.loads(STATUS.read_text(encoding="utf-8-sig"))
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def command(action):
    sequence = time.time_ns()
    pending = LOGS / "renderer-command.pending"
    pending.write_text(f"{sequence} {action}\n", encoding="ascii")
    pending.replace(LOGS / "goldcraft-test-command.txt")
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if read_status().get("testCommand", 0) >= sequence:
            return
        time.sleep(0.1)
    raise TimeoutError("Live B did not acknowledge the renderer command")


def summarize(frames):
    frames = [f for f in frames if f["interval"] > 0]
    if not frames:
        raise ValueError("Renderer returned no complete frame intervals")
    result = {"frames": len(frames), "focusedFrames": sum(f["focused"] for f in frames)}
    result["states"] = {key: sorted({f[key] for f in frames})
                        for key in ("deferred", "diagnostics", "shadows", "width", "height")}
    for field in ("origin", "angles"):
        result[field + "Range"] = [max(f[field][i] for f in frames) - min(f[field][i] for f in frames)
                                   for i in range(3)]
    result["stableCamera"] = max(result["originRange"]) < 0.1 and max(result["anglesRange"]) < 0.1
    result["fullyFocused"] = result["focusedFrames"] == len(frames)
    result["fpsComparisonEligible"] = result["fullyFocused"] and result["stableCamera"]
    result["observedFps"] = 1000 / statistics.mean(f["interval"] for f in frames)
    intervals = sorted((f["interval"] for f in frames), reverse=True)
    result["onePercentLowFps"] = 1000 / statistics.mean(intervals[:max(1, len(intervals) // 100)])
    result["framesOver20Ms"] = sum(t > 20 for t in intervals)
    result["framesOver50Ms"] = sum(t > 50 for t in intervals)
    for key in STAGES:
        if key not in frames[0]:
            continue
        values = sorted(f[key] for f in frames)
        result[key] = {"meanMs": statistics.mean(values), "medianMs": statistics.median(values),
                       "p95Ms": values[min(len(values) - 1, int(len(values) * 0.95))],
                       "p99Ms": values[min(len(values) - 1, int(len(values) * 0.99))],
                       "maxMs": values[-1]}
    gpu = [f for f in frames if f.get("gpuReady")]
    result["gpuFrames"] = len(gpu)
    result["gpu"] = {key.removeprefix("gpu_"): {
        "meanMs": statistics.mean(f[key] for f in gpu),
        "p95Ms": sorted(f[key] for f in gpu)[min(len(gpu) - 1, int(len(gpu) * 0.95))],
        "maxMs": max(f[key] for f in gpu)}
        for key in (gpu[0] if gpu else {}) if key.startswith("gpu_")}
    result["frameLimits"] = {key: sorted({f[key] for f in frames})
                             for key in ("fpsMax", "fpsOverride", "vsync", "mainViews") if key in frames[0]}
    result["meanCounts"] = {key: statistics.mean(f[key] for f in frames)
                            for key in frames[0] if key.endswith("Calls") or key in
                            ("shadowCullTests", "shadowCulled", "boneCacheHits", "boneCacheMisses",
                             "boneCacheAllocations", "boneCacheReuses", "boneCacheOverflows")}
    return result


def sample(label, deferred, diagnostics, seconds, interleave=False):
    population = server_population()
    command(f"render_options {deferred} {diagnostics}")
    time.sleep(2)  # Exclude the option transition/shader warm-up.
    state = read_status()
    if state.get("rendererDeferred") != deferred or state.get("rendererDiagnostics") != diagnostics:
        raise RuntimeError("Renderer did not apply the requested options")
    previous = PROFILE.stat().st_mtime_ns if PROFILE.exists() else 0
    command(f"render_profile {seconds} {int(interleave)}")
    deadline = time.monotonic() + seconds + 15
    while time.monotonic() < deadline:
        if PROFILE.exists() and PROFILE.stat().st_mtime_ns != previous:
            try:
                data = json.loads(PROFILE.read_text(encoding="utf-8-sig"))
            except json.JSONDecodeError:
                time.sleep(0.1)
                continue
            break
        time.sleep(0.2)
    else:
        raise TimeoutError("Renderer did not finish the bounded profile")
    result = {"label": label, "requested": {"deferred": deferred, "diagnostics": diagnostics},
              "populationBefore": population, "populationAfter": server_population(),
              "summary": summarize(data["frames"]), "profile": data}
    compact = {key: result["summary"][key] for key in ("frames", "observedFps", "fpsComparisonEligible",
                                                      "shadow", "scene", "lights", "present")}
    for key in ("pointShadow", "directionalShadow", "studioShadow", "meanCounts"):
        if key in result["summary"]:
            compact[key] = result["summary"][key]
    if interleave:
        # Compare CPU/GPU work on adjacent frames in one live run. Frame intervals
        # also contain engine throttling, so do not present these as FPS gains.
        result["alternating"] = {str(mode): summarize([f for f in data["frames"] if f.get("shadowCull") == mode])
                                 for mode in (0, 1)}
        compact["alternatingShadowMs"] = {mode: value["shadow"]["meanMs"]
                                          for mode, value in result["alternating"].items()}
    print(json.dumps({"label": label, **compact}), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, choices=range(3, 31), default=12)
    parser.add_argument("--label", default="deferred-comparison")
    parser.add_argument("--single", choices=("on", "off", "diagnostics"))
    parser.add_argument("--compare-culling", action="store_true")
    parser.add_argument("--interleave-culling", action="store_true")
    args = parser.parse_args()
    if not args.label or any(c not in "abcdefghijklmnopqrstuvwxyz0123456789-_" for c in args.label):
        parser.error("label must use lowercase letters, digits, - or _")
    cases = [("deferred-diagnostics", 1, 1, None), ("deferred-clean", 1, 0, None),
             ("forward-clean", 0, 0, None), ("deferred-repeat", 1, 0, None)]
    if args.single:
        cases = {"on": [("deferred-clean", 1, 0, None)], "off": [("forward-clean", 0, 0, None)],
                 "diagnostics": [("deferred-diagnostics", 1, 1, None)]}[args.single]
    if args.compare_culling:
        cases = [("culling-off", 1, 0, 0), ("culling-on", 1, 0, 1),
                 ("culling-off-repeat", 1, 0, 0), ("culling-on-repeat", 1, 0, 1)]
    if args.interleave_culling:
        cases = [("culling-alternating-frames", 1, 0, None)]
    report = {"scope": "Live B, no simulated input; inclusive CPU times and available asynchronous GPU timestamps (schema 2).",
              "rendererSha256": hashlib.sha256((ROOT / "sandbox/cs-client-b/Half-Life/cstrike/metahook/plugins/Renderer_AVX2.dll").read_bytes()).hexdigest(),
              "cases": []}
    try:
        for label, deferred, diagnostics, culling in cases:
            if culling is not None:
                command(f"render_shadow_cull {culling}")
                if read_status().get("rendererShadowCull") != culling:
                    raise RuntimeError("Shadow culling option was not applied")
            result = sample(label, deferred, diagnostics, args.seconds, args.interleave_culling)
            result["requested"]["shadowCasterCull"] = culling
            report["cases"].append(result)
    finally:
        # Leave the requested deferred path enabled; diagnostics stay off in play.
        try:
            command("render_options 1 0")
            if args.compare_culling:
                command("render_shadow_cull 1")
        finally:
            target = ROOT / "analysis/native-regressions" / f"{args.label}-{time.time_ns()}.json"
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(json.dumps(report, separators=(",", ":")), encoding="utf-8")
            print("Evidence: " + target.relative_to(ROOT).as_posix(), flush=True)


if __name__ == "__main__":
    main()
