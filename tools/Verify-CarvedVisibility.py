"""Record conservative carved PVS and immutable visible-command evidence.

This checks actual BSP data and hidden driver-backed GL, not live hw.dll
visibility, async leaf retirement, decals, static-shadow invalidation, entity
interest on the server or the coordinated collision/Renderer/Replica commit.
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
    native, native_names, native_log = surface.tests(ROOT / "build/native-clang-x86-Release", 22)
    renderer, renderer_names, renderer_log = surface.tests(ROOT / "build/renderer-clang-avx2-Release", 11)
    _, utility_names, utility_log = surface.tests(ROOT / "build/utilthreadtask-clang-Release", 10)
    assert "goldcraft_carved_visibility" in native_names

    def record(text, key):
        result = next(json.loads(line) for line in text.splitlines() if line.startswith('{"' + key + '"'))
        assert result["passed"]
        return result

    cpu = record(native, "carvedVisibility")
    assert cpu["spatialCuts"] == 4096 and cpu["queries"] == 262144 and cpu["queryAllocations"] == 0
    gpu = record(renderer, "cavityVisibilityGL")
    assert gpu["totalCommands"] == 2 and gpu["selectedCommands"] == 1
    assert gpu["selectedPixels"] == 128 and gpu["excludedPixels"] == 128
    assert gpu["emptyViewNoDraw"] and gpu["gpuFailureRecovered"] and gpu["retainsGeometry"]
    assert gpu["uploadsDuringDraw"] == 0 and gpu["gpuUsPerBatch"] > 0
    actual_map = record(renderer, "mapCarvedVisibility")
    assert actual_map["cases"] == 12 and actual_map["newSightlines"] > 0 and actual_map["boundedViews"] > 0
    layouts = record(renderer, "surfaceLayouts")
    assert layouts["engines"] == 5 and layouts["surfacesPerEngine"] == 3206
    assert layouts["hl25MapCandidates"] == 12 and layouts["identicalGeometryAndLighting"]
    lighting = record(renderer, "cavityLighting")
    assert lighting["productionShaders"] == 6 and lighting["litPixels"] == 256 and lighting["preservedPixels"] == 3840
    assert lighting["styleSwitch"] and lighting["gpuFailureRecovered"] and lighting["uploadsDuringDraw"] == 0
    geometry = record(renderer, "preservedPixels")
    assert geometry["preservedPixels"] == 3840 and geometry["restored"] and geometry["gpuFailureRecovered"]
    current = (surface.RENDERER / "src/gl_wsurf.cpp").read_text(encoding="utf-8-sig")
    upstream = subprocess.check_output(["git", "-C", str(surface.RENDERER), "show", "HEAD:src/gl_wsurf.cpp"],
                                      text=True, encoding="utf-8")
    assert surface.original_upload(current) == surface.original_upload(upstream)
    access = (surface.RENDERER / "src/world_surface_access.cpp").read_text(encoding="utf-8-sig")
    start = upstream.index("msurface_t* R_GetWorldSurfaceByIndex(")
    end = upstream.index("mleaf_t* R_GetWorldLeafByIndex(", start)
    assert upstream[start:end].strip() == access[access.index("msurface_t* R_GetWorldSurfaceByIndex("):].strip()
    sources = ["CMakeLists.txt", "sources.lock.json", "native/include/goldcraft/carved_visibility.hpp",
               "native/common/carved_visibility.cpp", "tests/native/carved_visibility_tests.cpp",
               "native/common/cavity_lighting.cpp", "native/common/cavity_mesh.cpp", "native/common/world_carving.cpp",
               "tools/Verify-CarvedVisibility.py", "tools/Verify-SurfaceMesh.py"]
    sources += ["external/MetaHookSv-20261007/Plugins/Renderer/" + name for name in (
        "src/world_surface_access.cpp", "src/gl_cavity_light.cpp", "src/gl_surface_edit.cpp", "src/gl_wsurf.h", "src/gl_wsurf.cpp",
        "cmake/Sources.cmake", "tests/CMakeLists.txt", "tests/surface_mesh_gl_tests.cpp", "tests/cavity_map_fixture.h",
        "tests/cavity_lighting_gl_tests.cpp", "assets/svencoop/renderer/shader/wsurf_shader.vert.glsl",
        "assets/svencoop/renderer/shader/wsurf_shader.frag.glsl")]
    inputs = [native_log, renderer_log, utility_log,
              ROOT / "analysis/world-carving/visibility-native-final.log",
              ROOT / "analysis/world-carving/visibility-renderer-final.log",
              ROOT / "build/native-x86/Release/goldcraft_carved_visibility_tests.exe",
              ROOT / "build/renderer-clang-avx2-Release/tests/cavity_lighting_gl_tests.exe",
              ROOT / "build/renderer-clang-avx2-Release/tests/surface_mesh_gl_tests.exe",
              ROOT / "build/renderer-clang-avx2-Release/Renderer_AVX2.dll",
              ROOT / "dist/renderer/svencoop/metahook/plugins/Renderer_AVX2.dll",
              ROOT / "sandbox/cs-client-b/Half-Life/cstrike/maps/cs_assault.bsp"]
    assert surface.digest(inputs[8]) == surface.digest(inputs[9]), "Staged Renderer differs from tested build"
    report = {"capturedAt": time.time(), "scope": __doc__, "nativeTests": native_names,
              "rendererTests": renderer_names, "utilityTests": utility_names,
              "cpu": cpu, "gpu": gpu, "map": actual_map, "surfaceLayouts": layouts,
              "lightingRegression": lighting, "geometryRegression": geometry,
              "originalUploadUnchanged": True, "surfaceAccessMovedUnchanged": True,
              "preservation": surface.preserve("visibility-preservation.json"),
              "sources": {name: surface.digest(ROOT / name) for name in sources},
              "inputs": {file.relative_to(ROOT).as_posix(): surface.digest(file) for file in inputs}}
    (ROOT / "analysis/world-carving/visibility-evidence.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"nativeTests": len(native_names), "rendererTests": len(renderer_names), "cpu": cpu,
                      "gpu": gpu, "map": actual_map, "surfaceLayouts": layouts,
                      "preservation": report["preservation"], "passed": True}))


if __name__ == "__main__":
    main()
