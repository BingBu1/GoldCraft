"""Capture executed client-event tests and preservation, without starting a game.

The optional native test calls reviewed functions from an identity-checked
engine image without DllMain. It is not graphical input/network/FPS acceptance.
"""
import importlib.util
import json
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parent.parent


def main():
    spec = importlib.util.spec_from_file_location("surface_evidence", ROOT / "tools/Verify-SurfaceMesh.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    text, names, log = helper.tests(ROOT / "build/native-clang-x86-Release", 23)
    lines = [json.loads(line) for line in text.splitlines() if line.startswith('{') and line.endswith('}')]
    engine = next(row for row in lines if row.get("actualEngineEventTrace"))
    assert engine["passed"] and engine["queries"] == 4096 and engine["noEditAllocations"] == 0
    assert engine["nativeEntityFilters"] and engine["engineContextIsolation"] and engine["restoreExact"]
    stats = [row for row in lines if "mapCollisionEventTraces" in row]
    assert len(stats) == 3 and stats[-1]["mapCollisionFailures"] == 0
    assert stats[-1]["mapCollisionEventTraces"] > engine["queries"]
    assert stats[0]["mapCollisionFailures"] == 16  # Deliberately injected failures.
    previous = json.loads((ROOT / "analysis/world-carving/material-evidence.json").read_text(encoding="utf-8"))
    for path, digest in previous["unchangedRendererSources"].items():
        assert helper.digest(ROOT / path) == digest, path
    sources = ["CMakeLists.txt", "native/client/map_collision.cpp", "native/client/map_collision.hpp",
               "native/client/plugin.cpp", "native/client/map_edit_transaction.cpp",
               "native/include/goldcraft/edited_hull.hpp", "native/common/edited_hull.cpp",
               "native/common/world_volume.cpp", "tests/native/map_collision_tests.cpp",
               "tools/Build-Native.ps1", "tools/Verify-EventTraces.py",
               "external/MetaHookSv-20261007/MetaHook/src/metahook.cpp",
               "external/MetaHookSv-20261007/MetaHook/include/HLSDK/common/event_api.h",
               "external/MetaHookSv-20261007/MetaHook/include/HLSDK/pm_shared/pm_defs.h"]
    inputs = ["build/native-x86/Release/GoldCraft.dll",
              "build/native-x86/Release/goldcraft_map_collision_tests.exe",
              "sandbox/cs-client-b/Half-Life/hw.dll", "sandbox/cs-client-b/Half-Life/cstrike/maps/cs_assault.bsp",
              "build/native-clang-x86-Release/compile_commands.json", str(log.relative_to(ROOT)),
              "analysis/world-carving/event-native-build.log", "analysis/world-carving/event-clang-audit.log",
              "analysis/world-carving/event-engine-analysis.json"]
    report = {"capturedAt": time.time(), "scope": __doc__, "nativeTests": names,
              "engine": engine, "collisionCounters": stats,
              "sources": {p: helper.digest(ROOT / p) for p in sources},
              "inputs": {p: helper.digest(ROOT / p) for p in inputs},
              "unchangedRendererSources": previous["unchangedRendererSources"],
              "preservation": helper.preserve("event-preservation.json"), "passed": True}
    (ROOT / "analysis/world-carving/event-evidence.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"nativeTests": len(names), "sources": len(sources), "inputs": len(inputs),
                      "preservation": report["preservation"], "engine": engine, "passed": True}))


if __name__ == "__main__":
    main()
