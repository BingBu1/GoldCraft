"""Capture immutable cavity geometry/material and real-GL resource evidence.

The shader used by the GL test reads attributes; it is not production lighting.
No live GoldSrc geometry/PVS/shadows, GCEdit commit or gameplay is accepted here.
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
    native, native_names, native_log = surface.tests(ROOT / "build/native-clang-x86-Release", 20)
    renderer, renderer_names, renderer_log = surface.tests(ROOT / "build/renderer-clang-avx2-Release", 10)
    _, utility_names, utility_log = surface.tests(ROOT / "build/utilthreadtask-clang-Release", 10)
    assert "goldcraft_cavity_mesh" in native_names and "goldcraft_world_carving_assault" in native_names
    assert "surface_mesh_gl_tests" in renderer_names
    gpu = next(json.loads(line) for line in renderer.splitlines() if line.startswith('{"preservedPixels"'))
    assert gpu["passed"] and gpu["preservedPixels"] == 3840 and gpu["removedPixels"] == 256
    assert gpu["cavityPixels"] == 256 and gpu["unbakedLighting"]
    assert gpu["restored"] and gpu["gpuFailureRecovered"]
    performance = next(json.loads(line) for line in renderer.splitlines() if line.startswith('{"cavityPerformance"'))
    assert performance["cuts"] == 64 and performance["drawsPerBatch"] == 1 and performance["batches"] == 256
    assert performance["uploadsDuringDraw"] == 0 and performance["gpuUsPerBatch"] > 0
    map_check = next(json.loads(line) for line in native.splitlines() if '"cavityCases"' in line)
    assert map_check["passed"] and map_check["cavityCases"] == 100 and map_check["triangles"] > 1000
    assert map_check["unbakedLighting"]
    current = (surface.RENDERER / "src/gl_wsurf.cpp").read_text(encoding="utf-8-sig")
    upstream = subprocess.check_output(["git", "-C", str(surface.RENDERER), "show", "HEAD:src/gl_wsurf.cpp"],
                                       text=True, encoding="utf-8")
    assert surface.original_upload(current) == surface.original_upload(upstream)
    sources = ["CMakeLists.txt", "native/include/goldcraft/cavity_mesh.hpp", "native/common/cavity_mesh.cpp",
               "native/include/goldcraft/surface_mesh.hpp", "native/common/surface_mesh.cpp",
               "native/common/world_carving.cpp", "tests/native/cavity_mesh_tests.cpp",
               "tests/native/world_carving_tests.cpp", "tools/Verify-SurfaceMesh.py", "tools/Verify-CavityMesh.py"]
    sources += ["external/MetaHookSv-20261007/Plugins/Renderer/" + name for name in (
        "cmake/Sources.cmake", "src/gl_wsurf.h", "src/gl_surface_edit.cpp",
        "tests/CMakeLists.txt", "tests/surface_mesh_gl_tests.cpp")]
    inputs = [native_log, renderer_log, utility_log,
              ROOT / "analysis/world-carving/cavity-native-final.log",
              ROOT / "analysis/world-carving/cavity-renderer-indirect.log",
              ROOT / "build/native-x86/Release/goldcraft_cavity_mesh_tests.exe",
              ROOT / "build/native-x86/Release/goldcraft_world_carving_tests.exe",
              ROOT / "build/renderer-clang-avx2-Release/tests/surface_mesh_gl_tests.exe",
              ROOT / "build/renderer-clang-avx2-Release/Renderer_AVX2.dll",
              ROOT / "dist/renderer/svencoop/metahook/plugins/Renderer_AVX2.dll",
              ROOT / "sandbox/cs-client-b/Half-Life/cstrike/maps/cs_assault.bsp"]
    assert surface.digest(inputs[8]) == surface.digest(inputs[9]), "Staged Renderer differs from tested build"
    report = {"capturedAt": time.time(), "scope": __doc__, "nativeTests": native_names,
              "rendererTests": renderer_names, "utilityTests": utility_names,
              "gpuReadback": gpu, "performance": performance, "mapCavityChecks": map_check, "originalUploadUnchanged": True,
              "preservation": surface.preserve("cavity-preservation.json"),
              "sources": {name: surface.digest(ROOT / name) for name in sources},
              "inputs": {file.relative_to(ROOT).as_posix(): surface.digest(file) for file in inputs}}
    (ROOT / "analysis/world-carving/cavity-evidence.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"nativeTests": len(native_names), "rendererTests": len(renderer_names),
                      "gpuReadback": gpu, "performance": performance, "mapCavityChecks": map_check,
                      "preservation": report["preservation"], "passed": True}))


if __name__ == "__main__":
    main()
