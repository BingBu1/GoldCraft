"""Record production leaf-cache, worker-lifetime and GL submission evidence.

Loaded model_t fixtures use actual cs_assault BSP data, not a running hw.dll.
The leaf draw test uses a state-color shader and narrow engine adapters; the
existing six real world-shader regressions are checked separately. This does
not accept live GCEdit/Renderer/collision transactions, decals, static-shadow
cache invalidation, server entity visibility, gameplay or actual game FPS.
"""
import importlib.util
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("surface_evidence", ROOT / "tools/Verify-SurfaceMesh.py")
surface = importlib.util.module_from_spec(spec)
spec.loader.exec_module(surface)


def main():
    _, native_names, native_log = surface.tests(ROOT / "build/native-clang-x86-Release", 22)
    renderer, renderer_names, renderer_log = surface.tests(ROOT / "build/renderer-clang-avx2-Release", 12)
    _, utility_names, utility_log = surface.tests(ROOT / "build/utilthreadtask-clang-Release", 10)
    assert "world_leaf_gl_tests" in renderer_names

    def record(key):
        result = next(json.loads(line) for line in renderer.splitlines() if line.startswith('{"' + key + '"'))
        assert result["passed"]
        return result

    loaded = record("loadedWorldVisibility")
    assert loaded["engines"] == 2 and loaded["leaves"] == 460 and loaded["worldNodes"] == 953
    assert loaded["allocatedNodes"] == 1619 and loaded["pvsBitsCompared"] == 423200
    assert loaded["rejections"] == 10 and loaded["pvsPaddingBounded"]
    leaf = record("worldLeafGL")
    assert leaf["cavityCommands"] == 4 and leaf["cavityBatches"] == 3
    assert leaf["cavityPixels"] == 256 and leaf["preservedPixels"] == 3840
    assert leaf["uploadsDuringDraw"] == 0 and leaf["allocationsDuringDraw"] == 0
    assert leaf["vaoBindsPerFrame"] == 4 and leaf["indirectBindsPerFrame"] == 4
    assert leaf["opaqueShadowSubmissions"] == 1 and leaf["alphaShadowBatches"] == 6
    for key in ("emptyLeafReady", "removedFaceOmitted", "sealedCavityView", "crossGenerationRejected",
                "gpuFailureRecovered", "workerFailureRecovered", "retireNonblocking", "lateUploadBlocked"):
        assert leaf[key], key
    assert leaf["unloadWaitMs"] >= 20 and leaf["workerObjectsReleased"] >= 14
    lighting = record("cavityLighting")
    assert lighting["productionShaders"] == 6 and lighting["litPixels"] == 256
    assert lighting["preservedPixels"] == 3840 and lighting["gpuFailureRecovered"]
    assert lighting["styleSwitch"] and lighting["uploadsDuringDraw"] == 0
    map_lighting = record("mapCavityLighting")
    assert map_lighting["cases"] == 12 and map_lighting["occluded"] > 0
    visibility = record("mapCarvedVisibility")
    assert visibility["cases"] == 12 and visibility["newSightlines"] > 0
    current = (surface.RENDERER / "src/gl_wsurf.cpp").read_text(encoding="utf-8-sig")
    upstream = subprocess.check_output(["git", "-C", str(surface.RENDERER), "show", "HEAD:src/gl_wsurf.cpp"],
                                      text=True, encoding="utf-8")
    assert surface.original_upload(current) == surface.original_upload(upstream)

    # Native geometry code did not change in this checkpoint. Keep its executed
    # test evidence and identify that provenance rather than calling it a rerun.
    previous = json.loads((ROOT / "analysis/world-carving/visibility-evidence.json").read_text())
    native_sources = {name: digest for name, digest in previous["sources"].items()
                      if name.startswith(("native/", "tests/native/"))}
    assert native_sources
    assert all(surface.digest(ROOT / name) == digest for name, digest in native_sources.items())
    sources = ["sources.lock.json", "publication.json", "tools/Verify-WorldLeaves.py", "tools/Verify-SurfaceMesh.py"]
    sources += ["external/MetaHookSv-20261007/Plugins/Renderer/" + name for name in (
        "src/world_leaf_build.cpp", "src/world_leaf_cache.cpp", "src/world_leaf_draw.cpp", "src/world_visibility_access.cpp",
        "src/world_surface_access.cpp", "src/gl_surface_edit.cpp", "src/gl_cavity_light.cpp", "src/gl_wsurf.cpp",
        "src/gl_wsurf.h", "src/gl_model.cpp", "src/gl_rmain.cpp", "src/gl_water.cpp", "src/exportfuncs.cpp", "cmake/Sources.cmake",
        "tests/CMakeLists.txt", "tests/world_leaf_gl_tests.cpp", "tests/cavity_lighting_gl_tests.cpp",
        "tests/cavity_map_fixture.h", "assets/svencoop/renderer/shader/wsurf_shader.vert.glsl",
        "assets/svencoop/renderer/shader/wsurf_shader.frag.glsl")]
    sources += ["external/MetaHookSv-20261007/MetaHook/src/metahook.cpp", "external/ReHLDS/rehlds/engine/cmodel.cpp"]
    inputs = [native_log, renderer_log, utility_log,
              ROOT / "analysis/world-carving/leaf-renderer-final.log",
              ROOT / "build/renderer-clang-avx2-Release/tests/world_leaf_gl_tests.exe",
              ROOT / "build/renderer-clang-avx2-Release/tests/cavity_lighting_gl_tests.exe",
              ROOT / "build/renderer-clang-avx2-Release/Renderer_AVX2.dll",
              ROOT / "dist/renderer/svencoop/metahook/plugins/Renderer_AVX2.dll",
              ROOT / "sandbox/cs-client-b/Half-Life/cstrike/maps/cs_assault.bsp"]
    assert surface.digest(inputs[6]) == surface.digest(inputs[7])
    report = {"capturedAt": time.time(), "scope": __doc__, "nativeRegression": "unchanged since visibility checkpoint",
              "nativeTests": native_names, "nativeSources": native_sources,
              "rendererTests": renderer_names, "utilityTests": utility_names,
              "loadedWorld": loaded, "leaf": leaf, "lightingRegression": lighting,
              "mapLightingRegression": map_lighting, "mapVisibilityRegression": visibility,
              "originalUploadUnchanged": True, "preservation": surface.preserve("leaf-preservation.json"),
              "sources": {name: surface.digest(ROOT / name) for name in sources},
              "inputs": {file.relative_to(ROOT).as_posix(): surface.digest(file) for file in inputs}}
    (ROOT / "analysis/world-carving/leaf-evidence.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"rendererTests": len(renderer_names), "utilityTests": len(utility_names),
                      "loadedWorld": loaded, "leaf": leaf, "preservation": report["preservation"], "passed": True}))


if __name__ == "__main__":
    main()
