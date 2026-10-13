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
STUDIO_COMMANDS = {"FaceCull": "render_studio_face_cull", "DepthExecutor": "render_studio_depth_executor"}
DEPTH_COUNTS = ("studioDepthFastMeshes", "studioDepthFallbackMeshes")


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
              "gpuUploadBytes", "gpuDraws", "depthOnlyShadowPasses", "particleVertices", "postEffectStages", "postEffectFailures",
              "hudBlank", "hudFrames", "hudBlankFrames", "hudBlankUploads", "hudBlankCompositesSkipped",
              "hudDraws", "hudTextureWidth", "hudTextureHeight",
              "sceneCullingApi", "sceneCullingEnabled", "shadowBoundsTests", "shadowBoundsCulled",
              "sceneStaticShadowsApi", "sceneStaticShadowsEnabled", "sceneStaticShadowsRegistered", "staticShadowCallbacks",
              "staticShadowDraws", "staticShadowSectionSkips", "staticShadowInvalidations",
              "staticShadowAlphaInvalidations", "staticShadowFailures",
              "staticShadowAlphaChangedPackets", "staticShadowAlphaUnreferencedPackets",
              "staticShadowAlphaAffectedSections", "staticShadowAlphaFallbacks",
              "lights", "sceneLightsApi", "sceneLightsSubmitted", "sceneLightsFresh",
              "rendererDeferred", "rendererShadowCull", "rendererDiagnostics", "transport",
              "rendererStudioFaceCull", "rendererStudioFaceCullAvailable",
              "rendererStudioDepthExecutor", "rendererStudioDepthExecutorAvailable")
    java_fields = ("world", "life", "controlling", "screen", "minecraftFps", "entityFrames",
                   "entityExport", "presentationIntervalMs", "presentationReadbackMs", "vixPostFailures", "hudGlError")
    return {"time": now, "statusAgeMs": native_age_ms, "javaStatusAgeMs": java_age_ms,
            "native": {k: native[k] for k in fields if k in native},
            "java": {k: java[k] for k in java_fields if k in java}, "javaPerformance": perf}


def paired_gameplay(sample):
    if not sample:
        return False
    native, java = sample["native"], sample["java"]
    return (native.get("viewReady") and native.get("world") not in (None, "0", 0)
            and native.get("world") == java.get("world")
            and native.get("playerLife", 0) > 0
            and native.get("playerLife") == native.get("serverPlayerLife") == java.get("life")
            and native.get("minecraftControl") == java.get("controlling")
            and not java.get("screen") and not native.get("formMenuVisible"))


def wait_stable(seconds):
    if not seconds:
        return
    deadline = time.monotonic() + seconds
    anchor = None
    since = 0
    while time.monotonic() < deadline:
        current = snapshot()
        state = current["native"] if current else {}
        if (not paired_gameplay(current) or not state.get("windowFocused")
                or not 0 <= current["statusAgeMs"] <= 1500 or not 0 <= current["javaStatusAgeMs"] <= 2500):
            anchor = None
        elif (anchor is None or max(delta(state, anchor)) >= .1
              or any(state.get(k) != anchor.get(k) for k in ("world", "playerLife", "minecraftControl"))):
            anchor, since = state, time.monotonic()
        elif time.monotonic() - since >= 3:
            return
        time.sleep(.25)
    raise TimeoutError("No paired gameplay view remained stationary in the foreground for three seconds; no profile started")


def studio_feature(state, feature):
    key = "rendererStudio" + feature
    value = state.get(key)
    if (state.get(key + "Available") is not True
            or type(value) not in (int, float) or value not in (0, 1)):
        raise ValueError(f"Studio {feature} requires the existing Renderer cvar and numeric 0/1 diagnostics")
    return int(value)


def studio_face_cull(state):
    return studio_feature(state, "FaceCull")


def stable_studio_feature(samples, frames, active, requested, feature):
    status_key, frame_key = "rendererStudio" + feature, "studio" + feature
    counts = DEPTH_COUNTS if feature == "DepthExecutor" else ()
    present = (any(any(key in s["native"] for key in
                      (status_key, status_key + "Available")) for s in samples)
               or any(any(key in frame for key in (frame_key, *counts)) for frame in frames))
    # Older runtimes are compatible only when this feature was not requested
    # and neither producer claims to expose it. Partial telemetry fails closed.
    if not present and requested is None:
        return True
    try:
        return (active is not None and bool(samples) and bool(frames)
                and all(studio_feature(s["native"], feature) == active for s in samples)
                and all(type(f.get(frame_key)) in (int, float) and f[frame_key] == active for f in frames)
                and all(type(f.get(key)) is int and f[key] >= 0 for f in frames for key in counts))
    except ValueError:
        return False


def stable_studio_face_cull(samples, frames, active, requested):
    return stable_studio_feature(samples, frames, active, requested, "FaceCull")


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
    parser.add_argument("--scene-static-shadows", type=int, choices=(0, 1),
                        help="Temporarily disable/enable MC static shadow caching for a same-view comparison")
    studio_modes = parser.add_mutually_exclusive_group()
    studio_modes.add_argument("--studio-shadow-face-cull", type=int, choices=(0, 1),
                        help="Temporarily set the existing Studio shadow face-culling cvar; restore its original value")
    studio_modes.add_argument("--studio-shadow-depth-executor", type=int, choices=(0, 1),
                        help="Temporarily set the existing Studio shadow depth executor; restore its original value")
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
    original_static = first["native"].get("sceneStaticShadowsEnabled")
    original_shadow_cull = first["native"].get("rendererShadowCull")
    requested_studio = {"FaceCull": args.studio_shadow_face_cull, "DepthExecutor": args.studio_shadow_depth_executor}
    original_studio = {}
    for feature, requested in requested_studio.items():
        try:
            original_studio[feature] = studio_feature(first["native"], feature)
        except ValueError:
            if requested is not None:
                raise
            original_studio[feature] = None
    if args.scene_cull is not None and (not first["native"].get("sceneCullingApi") or not isinstance(original_culling, bool)):
        raise RuntimeError("Scene culling comparison requires the live optional Renderer API and diagnostics")
    if args.scene_static_shadows is not None and (not first["native"].get("sceneStaticShadowsApi") or not isinstance(original_static, bool)):
        raise RuntimeError("Static shadow comparison requires the live optional Renderer API and diagnostics")
    samples = []
    profiles = {}
    restored = None
    culling_restored = None
    static_restored = None
    studio_restored = {feature: None for feature in requested_studio}
    capture_error = restore_error = None
    active_caps = original_caps.copy()
    active_culling = original_culling
    active_static = original_static
    active_studio = original_studio.copy()
    try:
        if args.scene_cull is not None:
            renderer.command(f"scene_cull {args.scene_cull}")
            active_culling = renderer.read_status().get("sceneCullingEnabled")
            if active_culling != bool(args.scene_cull):
                raise RuntimeError("Live scene culling toggle was not applied")
        if args.scene_static_shadows is not None:
            renderer.command(f"scene_static_shadows {args.scene_static_shadows}")
            active_static = renderer.read_status().get("sceneStaticShadowsEnabled")
            if active_static != bool(args.scene_static_shadows):
                raise RuntimeError("Live static shadow toggle was not applied")
        for feature, requested in requested_studio.items():
            if requested is not None:
                renderer.command(f"{STUDIO_COMMANDS[feature]} {requested}")
                active_studio[feature] = studio_feature(renderer.read_status(), feature)
                if active_studio[feature] != requested:
                    raise RuntimeError(f"Live Studio {feature} toggle was not applied")
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
        if args.scene_static_shadows is not None:
            try:
                renderer.command(f"scene_static_shadows {int(original_static)}")
                static_restored = renderer.read_status().get("sceneStaticShadowsEnabled") == original_static
            except Exception as error:
                static_restored = False
                restore_error = f"{restore_error or ''} static shadows: {error}".strip()
        for feature, requested in requested_studio.items():
            if requested is not None:
                try:
                    renderer.command(f"{STUDIO_COMMANDS[feature]} {original_studio[feature]}")
                    studio_restored[feature] = studio_feature(renderer.read_status(), feature) == original_studio[feature]
                except Exception as error:
                    studio_restored[feature] = False
                    restore_error = f"{restore_error or ''} Studio {feature}: {error}".strip()
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
    summary["activeStaticShadows"] = active_static
    summary["staticShadowsRestored"] = static_restored
    summary["stableStaticShadows"] = all(s["native"].get("sceneStaticShadowsEnabled") == active_static for s in samples)
    renderer_frames = profiles.get("renderer", {}).get("frames", [])
    for feature, requested in requested_studio.items():
        summary["requestedStudio" + feature] = requested
        summary["originalStudio" + feature] = original_studio[feature]
        summary["activeStudio" + feature] = active_studio[feature]
        summary["studio" + feature + "Restored"] = studio_restored[feature]
        summary["stableStudio" + feature] = stable_studio_feature(
            samples, renderer_frames, active_studio[feature], requested, feature)
    stable_context = (
        type(active_culling) is bool and type(active_static) is bool
        and type(original_shadow_cull) in (int, float) and original_shadow_cull in (0, 1)
        and summary["stableSceneCulling"] and summary["stableStaticShadows"]
        and all(type(s["native"].get("rendererShadowCull")) in (int, float)
                and s["native"]["rendererShadowCull"] == original_shadow_cull for s in samples)
        and bool(renderer_frames)
        and all(type(f.get("shadowCull")) in (int, float) and f["shadowCull"] == original_shadow_cull
                for f in renderer_frames))
    for feature in requested_studio:
        context = {"sceneCulling": active_culling, "staticShadows": active_static,
                   "rendererShadowCull": original_shadow_cull}
        for other, active in active_studio.items():
            if other != feature and active is not None:
                context["studio" + other] = active
        summary["studio" + feature + "Context"] = context
        summary["stableStudio" + feature + "Context"] = (stable_context and
            all(summary["stableStudio" + other] for other in requested_studio if other != feature)
            and (feature != "DepthExecutor" or active_studio["FaceCull"] is not None))
    summary["staticShadowCounters"] = {key: samples[-1]["native"][key] - samples[0]["native"][key]
        for key in ("staticShadowCallbacks", "staticShadowDraws", "staticShadowSectionSkips",
                    "staticShadowInvalidations", "staticShadowAlphaInvalidations", "staticShadowFailures",
                    "staticShadowAlphaChangedPackets", "staticShadowAlphaUnreferencedPackets",
                    "staticShadowAlphaAffectedSections", "staticShadowAlphaFallbacks")
        if key in samples[0]["native"] and key in samples[-1]["native"]}
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
    summary["pairedGameplay"] = all(paired_gameplay(s) for s in samples)
    summary["noRenderErrors"] = all(s["native"].get("glError") == 0 and s["java"].get("hudGlError") == 0 for s in samples)
    for side, key in (("native", "postEffectFailures"), ("java", "vixPostFailures")):
        counts = {s[side].get(key) for s in samples}
        summary["noRenderErrors"] &= len(counts) == 1 and None not in counts
    if "staticShadowFailures" in samples[0]["native"]:
        summary["noRenderErrors"] &= len({s["native"].get("staticShadowFailures") for s in samples}) == 1
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
        if not isinstance(reference, dict):
            reference = {}
        prior = reference.get("summary", {})
        studio_reference_matches = True
        for feature, requested in requested_studio.items():
            if requested is not None:
                prior_requested = prior.get("requestedStudio" + feature)
                studio_reference_matches &= (type(prior_requested) is int and prior_requested in (0, 1)
                    and prior.get("activeStudio" + feature) == prior_requested
                    and prior.get("stableStudio" + feature) is True
                    and prior.get("stableStudio" + feature + "Context") is True
                    and prior.get("studio" + feature + "Context") == summary["studio" + feature + "Context"])
            else:
                studio_reference_matches &= prior.get("activeStudio" + feature) == active_studio[feature]
        a, b = reference.get("renderAnchor"), report.get("renderAnchor")
        summary["referenceMatches"] = bool(a and b and reference.get("summary", {}).get("eligible") and
            a["world"] == b["world"] and max(delta(a, b)) < .1 and
            reference["summary"]["renderer"]["states"] == quality and
            reference["summary"].get("activeCaps") == active_caps and
            studio_reference_matches and
            reference["samples"][0]["native"].get("minecraftControl") == samples[0]["native"].get("minecraftControl"))
    summary["eligible"] = (summary["complete"] and summary["freshSamples"] and summary["stableLifecycle"] and summary["stableCaps"] and
                           summary["pairedGameplay"] and summary["noRenderErrors"] and summary["requiredPopulation"] and
                           summary["fullQuality"] and summary["stableQuality"] and
                           summary["stableSceneCulling"] and culling_restored is not False and
                           summary["stableStaticShadows"] and static_restored is not False and
                           all(summary["stableStudio" + feature] and studio_restored[feature] is not False
                               and (requested is None or summary["stableStudio" + feature + "Context"])
                               for feature, requested in requested_studio.items()) and
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
    if static_restored is False:
        raise RuntimeError("Static shadow restoration did not match the original value; inspect live B")
    for feature, restored_feature in studio_restored.items():
        if restored_feature is False:
            raise RuntimeError(f"Studio {feature} restoration did not match the original value; inspect live B")


if __name__ == "__main__":
    main()
