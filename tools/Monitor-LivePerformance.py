"""Bounded live B timing capture, without gameplay input or changing render options.

Reports foreground/camera/lifecycle eligibility separately from observed FPS.
Raw evidence is local to analysis; it is never publication content.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import statistics
import time

ROOT = Path(__file__).resolve().parent.parent
LOGS = ROOT / "sandbox/cs-client-b/logs"
OUTPUT = ROOT / "analysis/perf-200fps"


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def read(path):
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return None


def delta(a, b):
    position = max(abs(x-y) for x, y in zip(a["origin"], b["origin"]))
    angle = max(abs((x-y+180) % 360-180) for x, y in zip(a["viewAngles"], b["viewAngles"]))
    return position, angle


def snapshot():
    native = read(LOGS / "goldcraft-client-status.json")
    java = read(LOGS / "minecraft-client-status.json")
    perf = read(LOGS / "performance-MinecraftClient.json")
    if native is None or java is None:
        return None
    try:
        native_modified = (LOGS / "goldcraft-client-status.json").stat().st_mtime
        java_modified = (LOGS / "minecraft-client-status.json").stat().st_mtime
        now = time.time()
        native_age_ms = (now - native_modified) * 1000
        java_age_ms = (now - java_modified) * 1000
    except OSError:
        # Diagnostics are replaced by their producers; a concurrent rename is
        # a missing sample, not a reason to abort and discard the live profile.
        return None
    fields = ("world", "origin", "viewAngles", "viewReady", "windowFocused", "minecraftControl",
              "playerLife", "serverPlayerLife", "minecraftForm", "formMenuVisible", "fpsLimit", "vsync", "glError", "frameWorkMs",
              "hudWorkMs", "nativeWork", "fpsOverride", "benchmarkFramerate", "entityCount", "entityBatches", "entityFrames",
              "entityEffectVertices", "sections", "vertices", "drawFrames", "gpuUploads",
              "gpuUploadBytes", "gpuDraws", "particleVertices", "postEffectStages", "postEffectFailures",
              "hudBlank", "hudFrames", "hudBlankFrames", "hudBlankUploads", "hudBlankCompositesSkipped",
              "hudDraws", "hudTextureWidth", "hudTextureHeight",
              "sceneCullingApi", "sceneCullingEnabled", "shadowBoundsTests", "shadowBoundsCulled",
              "lights", "sceneLightsApi", "sceneLightsSubmitted", "sceneLightsFresh",
              "rendererDeferred", "rendererShadowCull", "rendererDiagnostics", "transport")
    java_fields = ("world", "life", "controlling", "screen", "minecraftFps", "entityFrames",
                   "entityExport", "presentationIntervalMs", "presentationReadbackMs", "vixPostFailures", "hudGlError")
    return {"time": now, "statusAgeMs": native_age_ms, "javaStatusAgeMs": java_age_ms,
            "native": {k: native[k] for k in fields if k in native},
            "java": {k: java[k] for k in java_fields if k in java}, "javaPerformance": perf}


def wait_stable(seconds):
    if not seconds:
        return
    deadline = time.monotonic() + seconds
    anchor = None
    since = 0
    while time.monotonic() < deadline:
        current = snapshot()
        state = current["native"] if current else {}
        if (not state.get("windowFocused") or not state.get("viewReady") or current["statusAgeMs"] > 1500):
            anchor = None
        elif anchor is None or max(delta(state, anchor)) >= .1:
            anchor, since = state, time.monotonic()
        elif time.monotonic() - since >= 3:
            return
        time.sleep(.25)
    raise TimeoutError("No foreground view remained stationary for three seconds; no profile started")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, choices=range(5, 31), default=20)
    parser.add_argument("--wait-stable", type=int, default=0)
    parser.add_argument("--label", default="live")
    parser.add_argument("--benchmark-fps", type=int, choices=(100, 200, 300),
                        help="Temporary paired-loopback benchmark cap; restored after capture")
    parser.add_argument("--reference", type=Path, help="Previous report whose anchor must match for a comparison")
    parser.add_argument("--scene-cull", type=int, choices=(0, 1),
                        help="Temporarily disable/enable MC shadow culling for a same-view comparison")
    args = parser.parse_args()
    if not args.label or any(c not in "abcdefghijklmnopqrstuvwxyz0123456789-_" for c in args.label):
        parser.error("label must use lowercase letters, digits, hyphens or underscores")
    if not 0 <= args.wait_stable <= 120:
        parser.error("wait-stable must be between 0 and 120 seconds")
    renderer = module("live_renderer", "Exercise-RendererPerformance.py")
    native = module("live_native", "Exercise-RendererReference.py")
    wait_stable(args.wait_stable)
    first = snapshot()
    if not first or first["statusAgeMs"] > 1500:
        raise RuntimeError("No fresh B diagnostics; start the sandbox before sampling")
    paths = {"renderer": renderer.PROFILE, "native": LOGS / "native-frame-profile.json"}
    old = {k: p.stat().st_mtime_ns if p.exists() else 0 for k, p in paths.items()}
    population_before = renderer.server_population()
    original_caps = {k: first["native"].get(k) for k in ("fpsLimit", "fpsOverride", "vsync")}
    original_culling = first["native"].get("sceneCullingEnabled")
    if args.scene_cull is not None and (not first["native"].get("sceneCullingApi") or not isinstance(original_culling, bool)):
        raise RuntimeError("Scene culling comparison requires the live optional Renderer API and diagnostics")
    samples = []
    profiles = {}
    restored = None
    culling_restored = None
    capture_error = restore_error = None
    active_caps = original_caps.copy()
    active_culling = original_culling
    try:
        if args.scene_cull is not None:
            renderer.command(f"scene_cull {args.scene_cull}")
            active_culling = renderer.read_status().get("sceneCullingEnabled")
            if active_culling != bool(args.scene_cull):
                raise RuntimeError("Live scene culling toggle was not applied")
        if args.benchmark_fps:
            if args.seconds > 25:
                raise ValueError("Benchmark samples must finish within the 30-second native lease")
            renderer.command(f"benchmark_framerate {args.benchmark_fps}")
            check = renderer.read_status()
            if not check.get("benchmarkFramerate") or check.get("fpsLimit") != args.benchmark_fps:
                raise RuntimeError("Benchmark rejected by the live paired-loopback guard")
        active_caps = {k: renderer.read_status().get(k) for k in original_caps}
        samples.append(snapshot() or first)
        # Profilers buffer in memory and flush once; no screenshot or quality changes.
        renderer.command(f"render_profile {args.seconds} 0")
        renderer.command(f"native_profile {args.seconds}")
        deadline = time.monotonic() + args.seconds + 12
        while time.monotonic() < deadline:
            sample = snapshot()
            if sample:
                samples.append(sample)
            for key, path in paths.items():
                if key not in profiles and path.exists() and path.stat().st_mtime_ns != old[key]:
                    value = read(path)
                    if value and value.get("frames"):
                        profiles[key] = value
            if len(profiles) == 2:
                break
            time.sleep(.25)
    except BaseException as error:
        capture_error = error
    finally:
        if args.benchmark_fps:
            try:
                renderer.command("benchmark_framerate 0")
                state = renderer.read_status()
                restored = {k: state.get(k) for k in original_caps} == original_caps and not state.get("benchmarkFramerate")
            except Exception as error:
                restored, restore_error = False, str(error)
        if args.scene_cull is not None:
            try:
                renderer.command(f"scene_cull {int(original_culling)}")
                culling_restored = renderer.read_status().get("sceneCullingEnabled") == original_culling
            except Exception as error:
                culling_restored = False
                restore_error = f"{restore_error or ''} scene culling: {error}".strip()
    if not samples:
        samples = [first]
    try:
        population_after = renderer.server_population()
    except Exception as error:
        population_after = {"error": str(error)}
    report = {"label": args.label, "profiles": profiles, "samples": samples,
              "captureError": str(capture_error) if capture_error else None, "restoreError": restore_error,
              "populationBefore": population_before, "populationAfter": population_after,
              "anchor": {k: first["native"].get(k) for k in ("world", "origin", "viewAngles")}}
    summary = {"complete": len(profiles) == 2, "sampleCount": len(samples)}
    summary["originalCaps"] = original_caps
    summary["activeCaps"] = active_caps
    summary["benchmarkRestored"] = restored
    summary["activeSceneCulling"] = active_culling
    summary["sceneCullingRestored"] = culling_restored
    summary["stableSceneCulling"] = all(s["native"].get("sceneCullingEnabled") == active_culling for s in samples)
    if "renderer" in profiles:
        summary["renderer"] = renderer.summarize(profiles["renderer"]["frames"])
        frame = next(f for f in profiles["renderer"]["frames"] if f["interval"] > 0)
        # GoldSrc's player origin and the actual eye position differ in MC form.
        # Compare rendered eye positions from the profiles, not a pre-capture status.
        report["renderAnchor"] = {"world": report["anchor"]["world"], "origin": frame["origin"], "viewAngles": frame["angles"]}
    if "native" in profiles:
        try:
            summary["native"] = native.summarize(profiles["native"]["frames"])
        except ValueError as error:
            summary["nativeError"] = str(error)
    summary["freshSamples"] = all(0 <= s["statusAgeMs"] <= 1500 and 0 <= s["javaStatusAgeMs"] <= 2500 for s in samples)
    summary["stableLifecycle"] = len({(s["native"].get("world"), s["java"].get("life"),
                                       s["java"].get("controlling"), s["java"].get("screen")) for s in samples}) == 1
    summary["referenceMatches"] = None
    summary["pairedGameplay"] = all(s["native"].get("viewReady") and s["native"].get("world") not in (None, "0", 0)
        and s["native"].get("world") == s["java"].get("world")
        and s["native"].get("playerLife", 0) > 0
        and s["native"].get("playerLife") == s["native"].get("serverPlayerLife") == s["java"].get("life")
        and s["native"].get("minecraftControl") == s["java"].get("controlling")
        and not s["java"].get("screen") and not s["native"].get("formMenuVisible") for s in samples)
    summary["noRenderErrors"] = all(s["native"].get("glError") == 0 and s["java"].get("hudGlError") == 0 for s in samples)
    for side, key in (("native", "postEffectFailures"), ("java", "vixPostFailures")):
        counts = {s[side].get(key) for s in samples}
        summary["noRenderErrors"] &= len(counts) == 1 and None not in counts
    summary["requiredPopulation"] = all(p.get("players") == 25 and p.get("bots") == 24
                                        for p in (report["populationBefore"], report["populationAfter"]))
    quality = summary.get("renderer", {}).get("states", {})
    summary["fullQuality"] = all(quality.get(k) == [v] for k, v in (("deferred", 1), ("diagnostics", 0), ("shadows", 1)))
    summary["stableQuality"] = bool(quality) and all(len(v) == 1 for v in quality.values())
    limits = summary.get("renderer", {}).get("frameLimits", {})
    summary["stableCaps"] = all(limits.get(frame) == [active_caps[state]] for frame, state in
                                (("fpsMax", "fpsLimit"), ("fpsOverride", "fpsOverride"), ("vsync", "vsync")))
    if args.reference:
        reference = read(args.reference)
        a, b = reference.get("renderAnchor"), report.get("renderAnchor")
        summary["referenceMatches"] = bool(a and b and reference.get("summary", {}).get("eligible") and
            a["world"] == b["world"] and max(delta(a, b)) < .1 and
            reference["summary"]["renderer"]["states"] == quality and
            reference["summary"].get("activeCaps") == active_caps and
            reference["samples"][0]["native"].get("minecraftControl") == samples[0]["native"].get("minecraftControl"))
    summary["eligible"] = (summary["complete"] and summary["freshSamples"] and summary["stableLifecycle"] and summary["stableCaps"] and
                           summary["pairedGameplay"] and summary["noRenderErrors"] and summary["requiredPopulation"] and
                           summary["fullQuality"] and summary["stableQuality"] and
                           summary["stableSceneCulling"] and culling_restored is not False and
                           summary.get("native", {}).get("foregroundFpsEligible", False) and
                           summary.get("renderer", {}).get("fpsComparisonEligible", False) and
                           summary["referenceMatches"] is not False and restored is not False and capture_error is None)
    for field in ("frameWorkMs", "hudWorkMs"):
        values = [s["native"][field] for s in samples if field in s["native"]]
        if values:
            summary[field] = {"meanMs": statistics.mean(values), "maxMs": max(values)}
    report["summary"] = summary
    OUTPUT.mkdir(parents=True, exist_ok=True)
    target = OUTPUT / f"{args.label}-{time.time_ns()}.json"
    target.write_text(json.dumps(report, separators=(",", ":")), encoding="utf-8")
    print(json.dumps(summary), flush=True)
    print(f"Evidence: {target}", flush=True)
    if capture_error is not None:
        raise capture_error
    if not summary["complete"]:
        raise RuntimeError("At least one profile did not finish; partial evidence preserved")
    if restored is False:
        raise RuntimeError("Benchmark cap restoration did not match the original values; inspect live B")
    if culling_restored is False:
        raise RuntimeError("Scene culling restoration did not match the original value; inspect live B")


if __name__ == "__main__":
    main()
