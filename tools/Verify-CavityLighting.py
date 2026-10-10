"""Record cavity irradiance preparation and actual world-shader GL evidence.

This approximate, occlusion-filtered extension of BSP luxels is not a RAD
recompile. Hidden GL checks do not prove live GCEdit/leaf/PVS/shadow invalidation,
moving brush lighting, gameplay excavation or in-game frame times.
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
    native, native_names, native_log = surface.tests(ROOT / "build/native-clang-x86-Release", 21)
    renderer, renderer_names, renderer_log = surface.tests(ROOT / "build/renderer-clang-avx2-Release", 11)
    _, utility_names, utility_log = surface.tests(ROOT / "build/utilthreadtask-clang-Release", 10)
    assert "goldcraft_cavity_lighting" in native_names and "goldcraft_cavity_mesh" in native_names
    assert "cavity_lighting_gl_tests" in renderer_names
    gpu = next(json.loads(line) for line in renderer.splitlines() if line.startswith('{"cavityLighting"'))
    assert gpu["passed"] and gpu["litPixels"] == 256 and gpu["preservedPixels"] == 3840
    assert gpu["productionShaders"] == 6 and gpu["styleSwitch"] and gpu["gpuFailureRecovered"]
    assert gpu["uploadsDuringDraw"] == 0 and gpu["gpuUsPerBatch"] > 0
    actual_map = next(json.loads(line) for line in renderer.splitlines() if line.startswith('{"mapCavityLighting"'))
    assert actual_map["passed"] and actual_map["cases"] == 12 and actual_map["crc32"] == "f6725c06"
    assert actual_map["styleValues"] > 0 and 0 < actual_map["occluded"] < actual_map["rays"]
    geometry = next(json.loads(line) for line in renderer.splitlines() if line.startswith('{"preservedPixels"'))
    assert geometry["passed"] and geometry["restored"] and geometry["gpuFailureRecovered"]
    current = (surface.RENDERER / "src/gl_wsurf.cpp").read_text(encoding="utf-8-sig")
    upstream = subprocess.check_output(["git", "-C", str(surface.RENDERER), "show", "HEAD:src/gl_wsurf.cpp"],
                                      text=True, encoding="utf-8")
    assert surface.original_upload(current) == surface.original_upload(upstream)
    sources = ["CMakeLists.txt", "sources.lock.json", "native/include/goldcraft/cavity_lighting.hpp",
               "native/common/cavity_lighting.cpp", "native/common/cavity_mesh.cpp",
               "native/include/goldcraft/cavity_mesh.hpp", "tests/native/cavity_lighting_tests.cpp",
               "tests/native/cavity_mesh_tests.cpp", "tools/Verify-CavityLighting.py", "tools/Verify-SurfaceMesh.py"]
    sources += ["external/MetaHookSv-20261007/Plugins/Renderer/" + name for name in (
        "src/gl_cavity_light.cpp", "src/gl_surface_edit.cpp", "src/gl_wsurf.h", "src/gl_wsurf.cpp", "src/gl_common.h",
        "cmake/Sources.cmake", "tests/CMakeLists.txt", "tests/cavity_lighting_gl_tests.cpp", "tests/cavity_map_fixture.h",
        "assets/svencoop/renderer/shader/common.h", "assets/svencoop/renderer/shader/wsurf_shader.vert.glsl",
        "assets/svencoop/renderer/shader/wsurf_shader.geom.glsl", "assets/svencoop/renderer/shader/wsurf_shader.frag.glsl")]
    inputs = [native_log, renderer_log, utility_log,
              ROOT / "analysis/world-carving/lighting-native-final.log",
              ROOT / "analysis/world-carving/lighting-renderer-final.log",
              ROOT / "build/native-x86/Release/goldcraft_cavity_lighting_tests.exe",
              ROOT / "build/renderer-clang-avx2-Release/tests/cavity_lighting_gl_tests.exe",
              ROOT / "build/renderer-clang-avx2-Release/Renderer_AVX2.dll",
              ROOT / "dist/renderer/svencoop/metahook/plugins/Renderer_AVX2.dll",
              ROOT / "sandbox/cs-client-b/Half-Life/cstrike/maps/cs_assault.bsp"]
    assert surface.digest(inputs[7]) == surface.digest(inputs[8]), "Staged Renderer differs from tested build"
    report = {"capturedAt": time.time(), "scope": __doc__, "nativeTests": native_names,
              "rendererTests": renderer_names, "utilityTests": utility_names, "gpu": gpu,
              "map": actual_map, "geometryRegression": geometry, "originalUploadUnchanged": True,
              "preservation": surface.preserve("lighting-preservation.json"),
              "sources": {name: surface.digest(ROOT / name) for name in sources},
              "inputs": {file.relative_to(ROOT).as_posix(): surface.digest(file) for file in inputs}}
    (ROOT / "analysis/world-carving/lighting-evidence.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"nativeTests": len(native_names), "rendererTests": len(renderer_names),
                      "gpu": gpu, "map": actual_map, "preservation": report["preservation"], "passed": True}))


if __name__ == "__main__":
    main()
