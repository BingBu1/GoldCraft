"""Measure the live B renderer without sending mouse, keyboard or gameplay input.

The Renderer stores bounded CPU-side elapsed timings and writes once per sample.
Nested scene/stage timings are inclusive. Focus/camera changes invalidate an FPS
comparison; CPU stage samples remain useful for identifying expensive work.
"""
import argparse
import hashlib
import importlib.util
import json
import math
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
          "studioBoneSetup", "studioBoneSave", "studioBoneMerge", "studioMesh", "studioSubmit",
          "studioUBOUpload", "shadowWorldPrep", "ringPoll", "ringWait", "studioDepthExecute", "studioBoneMergeLookup", "studioBoneMergeNative")


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
    except (OSError, json.JSONDecodeError):
        return {}


def command(action):
    sequence = time.time_ns()
    pending = LOGS / "renderer-command.pending"
    pending.write_text(f"{sequence} {action}\n", encoding="ascii")
    # The native reader briefly opens this file without delete sharing on
    # Windows. Retry the same unpublished command, with a bounded deadline.
    publish_deadline = time.monotonic() + 2
    while True:
        try:
            pending.replace(LOGS / "goldcraft-test-command.txt")
            break
        except PermissionError as error:
            if getattr(error, "winerror", None) not in (5, 32) or time.monotonic() >= publish_deadline:
                raise
            time.sleep(0.02)
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        observed = read_status().get("testCommand", 0)
        if observed == sequence:
            return
        if observed > sequence:
            raise RuntimeError("A concurrent sandbox command replaced the renderer command")
        time.sleep(0.1)
    raise TimeoutError("Live B did not acknowledge the renderer command")


def render_boundaries(frames):
    """Pair start-to-start intervals with the preceding frame's render span.

    Keep the zero-interval first frame: its render belongs to the next interval.
    Filtered/interleaved profiles cannot establish consecutive frame boundaries.
    These are wall-clock durations, not CPU attribution to the closed engine.
    """
    states = ("deferred", "diagnostics", "shadows", "width", "height", "shadowCull",
              "fpsMax", "fpsOverride", "vsync", "mainViews", "studioFaceCull",
              "studioDepthExecutor", "studioBonePaletteCache", "worldMaterialBatch",
              "drawParameterStream", "studioShadowPlan")
    result = {"pairs": 0, "rejected": {}, "focusedEndpoints": True,
              "stableCameraEndpoints": True, "stableStateEndpoints": True,
              "clockMeaning": "Wall-clock elapsed time; nested stages overlap. The gap is not engine CPU time."}
    intervals, renders, gaps = [], [], []
    camera_points = {field: [] for field in ("origin", "angles")}

    def number(value):
        return type(value) in (int, float) and math.isfinite(value)

    def reject(reason):
        result["rejected"][reason] = result["rejected"].get(reason, 0) + 1

    for index, current in enumerate(frames):
        interval = current.get("interval")
        if index == 0 and number(interval) and interval == 0:
            continue
        if not number(interval) or interval <= 0:
            reject("invalidInterval")
            continue
        if index == 0:
            reject("missingPrecedingFrame")
            continue
        previous = frames[index - 1]
        start, prior_start, render = current.get("time"), previous.get("time"), previous.get("render")
        if not all(number(value) for value in (start, prior_start, render)) or render < 0:
            reject("invalidTiming")
            continue
        # The producer serializes all times to four decimal places in ms.
        if start <= prior_start or abs(start - prior_start - interval) > 0.0002:
            reject("nonconsecutiveFrames")
            continue
        gap = interval - render
        if gap < -0.0002:
            reject("overlappingRender")
            continue
        intervals.append(interval)
        renders.append(render)
        gaps.append(gap)
        result["focusedEndpoints"] &= previous.get("focused") == 1 and current.get("focused") == 1
        camera_valid = all(
            isinstance(previous.get(field), (list, tuple)) and isinstance(current.get(field), (list, tuple))
            and len(previous[field]) == len(current[field]) == 3
            and all(number(a) and number(b) and abs(a - b) < 0.1
                    for a, b in zip(previous[field], current[field]))
            for field in ("origin", "angles"))
        result["stableCameraEndpoints"] &= camera_valid
        if camera_valid:
            for field in camera_points:
                camera_points[field].extend((previous[field], current[field]))
        result["stableStateEndpoints"] &= all(previous.get(key) == current.get(key) for key in states)
    result["pairs"] = len(intervals)
    result["complete"] = bool(intervals) and not result["rejected"]
    for points in camera_points.values():
        if points:
            result["stableCameraEndpoints"] &= all(
                max(point[i] for point in points) - min(point[i] for point in points) < 0.1
                for i in range(3))
    for key, values in (("interval", intervals), ("precedingRender", renders), ("betweenRender", gaps)):
        if values:
            ordered = sorted(values)
            result[key] = {"meanMs": statistics.mean(values), "minMs": ordered[0],
                           "p95Ms": ordered[min(len(ordered) - 1, int(len(ordered) * 0.95))],
                           "maxMs": ordered[-1]}
    return result


def summarize(frames):
    boundaries = render_boundaries(frames)
    frames = [f for f in frames if f["interval"] > 0]
    if not frames:
        raise ValueError("Renderer returned no complete frame intervals")
    result = {"frames": len(frames), "focusedFrames": sum(f["focused"] for f in frames)}
    result["states"] = {key: sorted({f[key] for f in frames})
                        for key in ("deferred", "diagnostics", "shadows", "width", "height")}
    for feature in ("studioFaceCull", "studioDepthExecutor", "studioBonePaletteCache", "worldMaterialBatch", "drawParameterStream", "studioShadowPlan"):
        if any(feature in frame for frame in frames):
            result[feature + "States"] = sorted({frame[feature] for frame in frames
                                                 if type(frame.get(feature)) in (int, float)})
            result[feature + "Complete"] = all(type(frame.get(feature)) in (int, float)
                                                and frame[feature] in (0, 1) for frame in frames)
    for field in ("origin", "angles"):
        result[field + "Range"] = [max(f[field][i] for f in frames) - min(f[field][i] for f in frames)
                                   for i in range(3)]
    result["stableCamera"] = max(result["originRange"]) < 0.1 and max(result["anglesRange"]) < 0.1
    result["fullyFocused"] = result["focusedFrames"] == len(frames)
    result["renderBoundaries"] = boundaries
    result["fpsComparisonEligible"] = (result["fullyFocused"] and result["stableCamera"]
        and boundaries["complete"] and boundaries["focusedEndpoints"]
        and boundaries["stableCameraEndpoints"] and boundaries["stableStateEndpoints"])
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
    depth_counts = ("studioDepthFastMeshes", "studioDepthFallbackMeshes")
    if any(key in frame for frame in frames for key in depth_counts):
        result["studioDepthMeshCountsComplete"] = all(type(frame.get(key)) is int and frame[key] >= 0
                                                      for frame in frames for key in depth_counts)
        if result["studioDepthMeshCountsComplete"]:
            result["meanCounts"].update({key: statistics.mean(f[key] for f in frames) for key in depth_counts})
    palette_counts = ("studioBonePaletteUploads", "studioBonePaletteReuses",
                      "studioBonePaletteUploadBytes", "studioBonePaletteFallbacks")
    if any(key in frame for frame in frames for key in palette_counts):
        result["studioBonePaletteCountsComplete"] = all(type(frame.get(key)) is int and frame[key] >= 0
                                                       for frame in frames for key in palette_counts)
        if result["studioBonePaletteCountsComplete"]:
            result["meanCounts"].update({key: statistics.mean(f[key] for f in frames) for key in palette_counts})
    merge_counts = ("studioBoneMergeHits", "studioBoneMergeMisses", "studioBoneMergeBypasses")
    if any(key in frame for frame in frames for key in merge_counts):
        result["studioBoneMergeCountsComplete"] = all(type(frame.get(key)) is int and frame[key] >= 0
                                                      for frame in frames for key in merge_counts)
        if result["studioBoneMergeCountsComplete"]:
            result["meanCounts"].update({key: statistics.mean(f[key] for f in frames) for key in merge_counts})
            result["studioBoneMergeAccountingConsistent"] = all(
                sum(f[key] for key in merge_counts) == f.get("studioBoneMergeCalls")
                and f["studioBoneMergeHits"] + f["studioBoneMergeMisses"] == f.get("studioBoneMergeLookupCalls")
                and f["studioBoneMergeMisses"] + f["studioBoneMergeBypasses"] == f.get("studioBoneMergeNativeCalls")
                for f in frames)
    plan_counts = ("studioShadowPlanBuilds", "studioShadowPlanReuses",
                   "studioShadowPlanInvalidations", "studioShadowPlanFallbackModels")
    if any(key in frame for frame in frames for key in plan_counts):
        result["studioShadowPlanCountsComplete"] = all(type(frame.get(key)) is int and frame[key] >= 0
                                                     for frame in frames for key in plan_counts)
        if result["studioShadowPlanCountsComplete"]:
            result["meanCounts"].update({key: statistics.mean(f[key] for f in frames) for key in plan_counts})
    parameter_counts = ("drawParameterUploads", "drawParameterBytes", "drawParameterFallbacks",
                        "drawParameterBusyFallbacks", "drawParameterSyncFailures")
    if any(key in frame for frame in frames for key in parameter_counts):
        result["drawParameterCountsComplete"] = all(type(frame.get(key)) is int and frame[key] >= 0
                                                   for frame in frames for key in parameter_counts)
        if result["drawParameterCountsComplete"]:
            result["meanCounts"].update({key: statistics.mean(f[key] for f in frames) for key in parameter_counts})
    world_counts = ("worldMaterialChains", "worldMaterialSubmissions", "worldMaterialBatches")
    if any(key in frame for frame in frames for key in world_counts):
        result["worldMaterialCountsComplete"] = all(type(frame.get(key)) is int and frame[key] >= 0
                                                    for frame in frames for key in world_counts)
        if result["worldMaterialCountsComplete"]:
            result["meanCounts"].update({key: statistics.mean(f[key] for f in frames) for key in world_counts})
            result["worldMaterialAccountingConsistent"] = all(
                f["worldMaterialBatches"] <= f["worldMaterialSubmissions"] <= f["worldMaterialChains"]
                and f["worldMaterialChains"] <= f["worldMaterialSubmissions"] + 5*f["worldMaterialBatches"]
                for f in frames)
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
