"""Capture world-edit publication, decal clipping and cache-invalidation evidence.

The native transaction test uses the real collision adapter with a Renderer
dispatch spy. The GL test calls the real exported edit interface against loaded
model fixtures and real leaf workers/buffers, with narrow engine adapters and
state-color shader. Shadow tests verify real invalidation policy with readiness
spies, not rebaked six-face images. Neither proves actual hw.dll networking,
gameplay FPS, dynamic BSP edits, server entity PVS or full mode2 excavation.
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
    renderer, renderer_names, renderer_log = surface.tests(ROOT / "build/renderer-clang-avx2-Release", 12)
    _, utility_names, utility_log = surface.tests(ROOT / "build/utilthreadtask-clang-Release", 10)

    def record(log, key):
        result = next(json.loads(line) for line in log.splitlines() if line.startswith('{"' + key + '"'))
        assert result["passed"]
        return result

    client = record(native, "renderCollisionTransaction")
    for key in ("waitKeepsOldCollision", "failedCommitKeepsOld", "coalescesDeltas", "failureResync", "restore", "dynamicRejectsAtomically"):
        assert client[key], key
    edit = record(renderer, "worldEditGL")
    for key in ("publicInterface", "lateInlineModel", "selectiveShadowInvalidation", "restore", "cancel", "unload"):
        assert edit[key], key
    for key in ("commitAllocations", "commitUploads", "unchangedDecalAllocations", "unchangedDecalUploads"):
        assert edit[key] == 0, key
    assert edit["decalPixels"] == 3840 and edit["removedDecalPixels"] == 256 and edit["decalIndices"] > 96
    leaf = record(renderer, "worldLeafGL")
    assert leaf["allocationsDuringDraw"] == leaf["uploadsDuringDraw"] == 0
    lighting = record(renderer, "cavityLighting")
    assert lighting["productionShaders"] == 6 and lighting["gpuFailureRecovered"]
    loaded = record(renderer, "loadedWorldVisibility")
    assert loaded["pvsBitsCompared"] == 423200
    current = (surface.RENDERER / "src/gl_wsurf.cpp").read_text(encoding="utf-8-sig")
    upstream = subprocess.check_output(["git", "-C", str(surface.RENDERER), "show", "HEAD:src/gl_wsurf.cpp"], text=True, encoding="utf-8")
    assert surface.original_upload(current) == surface.original_upload(upstream)
    assert not subprocess.check_output(["git", "-C", str(surface.RENDERER), "diff", "--", "include/Interface/IMetaRenderer.h"])

    previous = json.loads((ROOT / "analysis/world-carving/leaf-evidence.json").read_text())
    sources = set(previous["sources"]) | set(previous["nativeSources"])
    sources.update("native/client/" + name for name in ("map_edit_transaction.cpp", "map_edit_transaction.hpp", "map_collision.cpp",
                   "map_collision.hpp", "plugin.cpp", "render_backend.cpp", "render_backend.hpp"))
    sources.update("external/MetaHookSv-20261007/Plugins/Renderer/" + name for name in (
        "include/Interface/IMetaRendererWorldEdit.h", "src/world_edit.cpp", "src/world_edit_cache.cpp", "src/gl_rsurf.cpp"))
    sources.update(("CMakeLists.txt", "tests/native/map_collision_tests.cpp", "tools/Verify-WorldTransaction.py"))
    inputs = [native_log, renderer_log, utility_log,
              ROOT / "analysis/world-carving/transaction-renderer-final.log",
              ROOT / "analysis/world-carving/transaction-native-final.log",
              ROOT / "build/renderer-clang-avx2-Release/tests/world_leaf_gl_tests.exe",
              ROOT / "build/native-x86/Release/goldcraft_map_collision_tests.exe",
              ROOT / "build/native-x86/Release/GoldCraft.dll",
              ROOT / "build/renderer-clang-avx2-Release/Renderer_AVX2.dll",
              ROOT / "dist/renderer/svencoop/metahook/plugins/Renderer_AVX2.dll",
              ROOT / "sandbox/cs-client-b/Half-Life/cstrike/maps/cs_assault.bsp"]
    assert surface.digest(inputs[8]) == surface.digest(inputs[9])
    report = {"capturedAt": time.time(), "scope": __doc__, "nativeTests": native_names,
              "rendererTests": renderer_names, "utilityTests": utility_names,
              "transaction": client, "worldEdit": edit, "leafRegression": leaf,
              "lightingRegression": lighting, "loadedWorld": loaded, "originalUploadUnchanged": True,
              "existingRendererAbiUnchanged": True, "preservation": surface.preserve("transaction-preservation.json"),
              "sources": {name: surface.digest(ROOT / name) for name in sorted(sources)},
              "inputs": {file.relative_to(ROOT).as_posix(): surface.digest(file) for file in inputs}}
    (ROOT / "analysis/world-carving/transaction-evidence.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"nativeTests": len(native_names), "rendererTests": len(renderer_names), "utilityTests": len(utility_names),
                      "transaction": client, "worldEdit": edit, "preservation": report["preservation"], "passed": True}))


if __name__ == "__main__":
    main()
