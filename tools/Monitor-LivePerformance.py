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
              "rendererStudioFaceCull", "rendererStudioFaceCullAvailable")
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


def studio_face_cull(state):
    value = state.get("rendererStudioFaceCull")
    if (state.get("rendererStudioFaceCullAvailable") is not True
            or type(value) not in (int, float) or value not in (0, 1)):
        raise ValueError("Studio face culling requires the existing Renderer cvar and numeric 0/1 diagnostics")
    return int(value)


def stable_studio_face_cull(samples, frames, active, requested):
    present = (any(any(key in s["native"] for key in
                      ("rendererStudioFaceCull", "rendererStudioFaceCullAvailable")) for s in samples)
               or any("studioFaceCull" in frame for frame in frames))
    # Older runtimes are compatible only when this feature was not requested
    # and neither producer claims to expose it. Partial telemetry fails closed.
    if not present and requested is None:
        return True
    try:
        return (active is not None and bool(samples) and bool(frames)
                and all(studio_face_cull(s["native"]) == active for s in samples)
                and all(type(f.get("studioFaceCull")) in (int, float)
                        and f["studioFaceCull"] == active for f in frames))
    except ValueError:
        return False


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
    parser.add_argument("--studio-shadow-face-cull", type=int, choices=(0, 1),
                        help="Temporarily set the existing Studio shadow face-culling cvar; restore its original value")
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
    try:
        original_face_cull = studio_face_cull(first["native"])
    except ValueError:
        if args.studio_shadow_face_cull is not None:
            raise
        original_face_cull = None
    if args.scene_cull is not None and (not first["native"].get("sceneCullingApi") or not isinstance(original_culling, bool)):
        raise RuntimeError("Scene culling comparison requires the live optional Renderer API and diagnostics")
    if args.scene_static_shadows is not None and (not first["native"].get("sceneStaticShadowsApi") or not isinstance(original_static, bool)):
        raise RuntimeError("Static shadow comparison requires the live optional Renderer API and diagnostics")
    samples = []
    profiles = {}
    restored = None
    culling_restored = None
    static_restored = None
    face_cull_restored = None
    capture_error = restore_error = None
    active_caps = original_caps.copy()
    active_culling = original_culling
    active_static = original_static
    active_face_cull = original_face_cull
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
        if args.studio_shadow_face_cull is not None:
            renderer.command(f"render_studio_face_cull {args.studio_shadow_face_cull}")
            active_face_cull = studio_face_cull(renderer.read_status())
            if active_face_cull != args.studio_shadow_face_cull:
                raise RuntimeError("Live Studio face-culling toggle was not applied")
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
        if args.studio_shadow_face_cull is not None:
            try:
                renderer.command(f"render_studio_face_cull {original_face_cull}")
                face_cull_restored = studio_face_cull(renderer.read_status()) == original_face_cull
            except Exception as error:
                face_cull_restored = False
                restore_error = f"{restore_error or ''} Studio face culling: {error}".strip()
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
    summary["requestedStudioFaceCull"] = args.studio_shadow_face_cull
    summary["originalStudioFaceCull"] = original_face_cull
    summary["activeStudioFaceCull"] = active_face_cull
    summary["studioFaceCullRestored"] = face_cull_restored
    summary["stableStudioFaceCull"] = stable_studio_face_cull(
        samples, profiles.get("renderer", {}).get("frames", []), active_face_cull, args.studio_shadow_face_cull)
    summary["studioFaceCullContext"] = {"sceneCulling": active_culling, "staticShadows": active_static,
                                        "rendererShadowCull": original_shadow_cull}
    summary["stableStudioFaceCullContext"] = (
        type(active_culling) is bool and type(active_static) is bool
        and type(original_shadow_cull) in (int, float) and original_shadow_cull in (0, 1)
        and summary["stableSceneCulling"] and summary["stableStaticShadows"]
        and all(type(s["native"].get("rendererShadowCull")) in (int, float)
                and s["native"]["rendererShadowCull"] == original_shadow_cull for s in samples)
        and bool(profiles.get("renderer", {}).get("frames"))
        and all(type(f.get("shadowCull")) in (int, float) and f["shadowCull"] == original_shadow_cull
                for f in profiles.get("renderer", {}).get("frames", [])))
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
        if args.studio_shadow_face_cull is not None:
            face_reference_matches = (type(prior.get("requestedStudioFaceCull")) is int
                                      and prior["requestedStudioFaceCull"] in (0, 1)
                                      and prior.get("activeStudioFaceCull") == prior["requestedStudioFaceCull"]
                                      and prior.get("stableStudioFaceCull") is True
                                      and prior.get("stableStudioFaceCullContext") is True
                                      and prior.get("studioFaceCullContext") == summary["studioFaceCullContext"])
        else:
            face_reference_matches = prior.get("activeStudioFaceCull") == active_face_cull
        a, b = reference.get("renderAnchor"), report.get("renderAnchor")
        summary["referenceMatches"] = bool(a and b and reference.get("summary", {}).get("eligible") and
            a["world"] == b["world"] and max(delta(a, b)) < .1 and
            reference["summary"]["renderer"]["states"] == quality and
            reference["summary"].get("activeCaps") == active_caps and
            face_reference_matches and
            reference["samples"][0]["native"].get("minecraftControl") == samples[0]["native"].get("minecraftControl"))
    summary["eligible"] = (summary["complete"] and summary["freshSamples"] and summary["stableLifecycle"] and summary["stableCaps"] and
                           summary["pairedGameplay"] and summary["noRenderErrors"] and summary["requiredPopulation"] and
                           summary["fullQuality"] and summary["stableQuality"] and
                           summary["stableSceneCulling"] and culling_restored is not False and
                           summary["stableStaticShadows"] and static_restored is not False and
                           summary["stableStudioFaceCull"] and face_cull_restored is not False and
                           (args.studio_shadow_face_cull is None or summary["stableStudioFaceCullContext"]) and
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
    if face_cull_restored is False:
        raise RuntimeError("Studio face-culling restoration did not match the original value; inspect live B")


if __name__ == "__main__":
    main()
