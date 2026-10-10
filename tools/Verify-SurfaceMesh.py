"""Capture exact build evidence for remaining-surface GPU preparation.

This verifies standalone meshes/driver-backed tests, not gameplay excavation,
PVS, cavity material/lighting, client input, or actual hw.dll rendering.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
RENDERER = ROOT / "external/MetaHookSv-20261007/Plugins/Renderer"


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def tests(directory, count):
    log = directory / "Testing/Temporary/LastTest.log"
    data = log.read_bytes()
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        # CTest uses the Windows console locale for its localized timestamps.
        text = data.decode("gb18030")
    text = text.replace("\r\n", "\n")
    assert text.count("Test Passed.") == count and "Test Failed." not in text, directory.name
    # Even ctest --show-only rewrites LastTest.log. Read the executed names from
    # the captured log so verification does not destroy its own input evidence.
    names = re.findall(r"^\d+/\d+ Test: (.+)$", text, re.MULTILINE)
    assert len(names) == count and len(set(names)) == count, directory.name
    return text, names, log


def original_upload(text):
    at = text.index("std::shared_ptr<CWorldSurfaceWorldModel> R_GenerateWorldSurfaceWorldModel(")
    at = text.index("    pWorldModel->hEBO = GL_GenBuffer();", at)
    return text[at:text.index("\n    return pWorldModel;", at)].replace("\r\n", "\n")


def preserve(output="surface-preservation.json"):
    baseline = json.loads((ROOT / "analysis/world-carving/mc-collision-before.json").read_text())
    settings = json.loads((ROOT / "settings.local.json").read_text(encoding="utf-8-sig"))
    original = Path(settings["originalGame"]).resolve()
    assert not original.is_relative_to(ROOT), "Unexpected original installation root"
    changed = []

    def check(base, records, label, full_inventory=False):
        if full_inventory:
            files = {file.relative_to(base).as_posix() for file in base.rglob("*") if file.is_file()}
            changed.extend(label + "/" + name for name in sorted(files ^ set(records)))
        for name, recorded in records.items():
            file = base / name
            assert file.resolve().is_relative_to(base.resolve()), "Escaped preservation path"
            if not file.is_file():
                changed.append(label + "/" + name)
                continue
            info = file.stat()
            actual = {"size": info.st_size, "mtime_ns": info.st_mtime_ns, "sha256": digest(file)}
            if actual != recorded:
                changed.append(label + "/" + name)

    check(original, baseline["original"], "original", True)
    for role, records in baseline["runtime"].items():
        check(ROOT / "sandbox" / role, records, role)
    result = {"original": len(baseline["original"]),
              "runtime": sum(len(rows) for rows in baseline["runtime"].values()), "changed": changed}
    (ROOT / "analysis/world-carving" / output).write_text(json.dumps(result, indent=2) + "\n")
    assert not changed, "Preservation differences; inspect local evidence"
    return result


def main():
    native, native_names, native_log = tests(ROOT / "build/native-clang-x86-Release", 19)
    renderer, renderer_names, renderer_log = tests(ROOT / "build/renderer-clang-avx2-Release", 10)
    assert "goldcraft_surface_mesh" in native_names and "goldcraft_world_carving_assault" in native_names
    assert "surface_mesh_gl_tests" in renderer_names
    gpu = next(json.loads(line) for line in renderer.splitlines() if line.startswith('{"preservedPixels"'))
    assert gpu["passed"] and gpu["preservedPixels"] == 3840 and gpu["removedPixels"] == 256
    assert gpu["restored"] and gpu["gpuFailureRecovered"]
    map_check = next(json.loads(line) for line in native.splitlines() if line.startswith('{"map":"cs_assault","crc32"'))
    assert map_check["passed"] and map_check["triangles"] > 1000
    current = (RENDERER / "src/gl_wsurf.cpp").read_text(encoding="utf-8-sig")
    upstream = subprocess.check_output(["git", "-C", str(RENDERER), "show", "HEAD:src/gl_wsurf.cpp"], text=True, encoding="utf-8")
    assert original_upload(current) == original_upload(upstream), "Ordinary map GPU upload path changed"
    sources = ["CMakeLists.txt", "native/include/goldcraft/surface_mesh.hpp", "native/common/surface_mesh.cpp",
               "native/common/world_carving.cpp", "tests/native/surface_mesh_tests.cpp", "tests/native/world_carving_tests.cpp",
               "sources.lock.json"]
    sources += ["external/MetaHookSv-20261007/Plugins/Renderer/" + name for name in (
        "CMakeLists.txt", "cmake/Sources.cmake", "src/gl_common.h", "src/gl_wsurf.h", "src/gl_wsurf.cpp",
        "src/gl_surface_edit.cpp", "tests/CMakeLists.txt", "tests/surface_mesh_gl_tests.cpp")]
    inputs = [native_log, renderer_log,
              ROOT / "analysis/world-carving/surface-native-final.log",
              ROOT / "analysis/world-carving/surface-renderer-final.log",
              ROOT / "build/native-x86/Release/goldcraft_surface_mesh_tests.exe",
              ROOT / "build/native-x86/Release/goldcraft_world_carving_tests.exe",
              ROOT / "build/renderer-clang-avx2-Release/tests/surface_mesh_gl_tests.exe",
              ROOT / "build/renderer-clang-avx2-Release/Renderer_AVX2.dll",
              ROOT / "dist/renderer/svencoop/metahook/plugins/Renderer_AVX2.dll",
              ROOT / "sandbox/cs-client-b/Half-Life/cstrike/maps/cs_assault.bsp"]
    assert digest(inputs[7]) == digest(inputs[8]), "Staged Renderer differs from the tested build"
    report = {"capturedAt": time.time(), "scope": __doc__, "nativeTests": native_names,
              "rendererTests": renderer_names, "gpuReadback": gpu, "mapSurfaceChecks": map_check,
              "originalUploadUnchanged": True, "preservation": preserve(),
              "sources": {name: digest(ROOT / name) for name in sources},
              "inputs": {file.relative_to(ROOT).as_posix(): digest(file) for file in inputs}}
    (ROOT / "analysis/world-carving/surface-evidence.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"nativeTests": len(native_names), "rendererTests": len(renderer_names),
                      "gpuReadback": gpu, "originalUploadUnchanged": True,
                      "preservation": report["preservation"], "passed": True}))


if __name__ == "__main__":
    main()
