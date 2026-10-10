"""Record integrated client brush collision tests without starting CS.

The fixture maps the identity-checked engine without DllMain and supplies PM,
model and entity state. It does not validate B input, UDP, rendering or FPS.
"""
import importlib.util
import json
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "analysis/brush-collision"


def main():
    spec = importlib.util.spec_from_file_location("evidence", ROOT / "tools/Verify-SurfaceMesh.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    text, names, log = helper.tests(ROOT / "build/native-clang-x86-Release", 30)
    rows = [json.loads(line) for line in text.splitlines() if line.startswith('{') and line.endswith('}')]
    dynamic = next(row for row in rows if row.get("actualEngineBrushTrace"))
    assert dynamic["passed"] and dynamic["actualPublicModelEntityLookups"]
    assert dynamic["identity1024HighSlot"] and dynamic["exceptionHullRestored"]
    assert dynamic["predicateOrder"] and dynamic["identityLifecycle"] and dynamic["nestedQueries"]
    assert dynamic["warmQueries"] == 4096 and dynamic["warmAllocations"] == 0
    event = next(row for row in rows if row.get("actualEngineEventTrace"))
    assert event["passed"] and event["engineContextIsolation"] and event["restoreExact"]
    counters = [row for row in rows if "mapCollisionEventTraces" in row]
    assert len(counters) == 4
    assert counters[2]["mapCollisionFailures"] == 0
    assert counters[3]["mapCollisionFailures"] == 15  # Deliberate generation/callback failures.

    previous = json.loads((ROOT / "analysis/packet-entities/evidence.json").read_text(encoding="utf-8"))
    unchanged = previous["unchangedGlSources"]
    for name, expected in unchanged.items():
        assert helper.digest(ROOT / name) == expected, name
    commands = json.loads((ROOT / "build/native-clang-x86-Release/compile_commands.json").read_text())
    sources = {Path(row["file"]).resolve().relative_to(ROOT).as_posix() for row in commands
               if Path(row["file"]).resolve().is_relative_to(ROOT / "native") or
                  Path(row["file"]).resolve().is_relative_to(ROOT / "tests")}
    sources.update(p.relative_to(ROOT).as_posix() for p in (ROOT / "native").rglob("*.hpp"))
    sources.update(("CMakeLists.txt", "tests/native/map_brush_collision_tests.inc", "sources.lock.json",
                    "tools/Build-Native.ps1", "tools/Verify-BrushCollision.py", "tools/ClangToolchain.ps1"))
    header_base = "external/MetaHookSv-20261007/MetaHook/include/HLSDK/"
    sources.update(header_base + name for name in (
        "engine/APIProxy.h", "common/com_model.h", "common/cl_entity.h", "common/event_api.h",
        "pm_shared/pm_defs.h"))
    inputs = ["build/native-x86/Release/GoldCraft.dll",
              "build/native-x86/Release/goldcraft_map_collision_tests.exe",
              "build/native-clang-x86-Release/compile_commands.json", log.relative_to(ROOT).as_posix(),
              "sandbox/cs-client-b/Half-Life/hw.dll",
              "sandbox/cs-client-b/Half-Life/cstrike/maps/cs_assault.bsp",
              "analysis/brush-collision/native-integration-final-build.log",
              "analysis/brush-collision/integration-clang-audit.json",
              "analysis/brush-collision/integration-clang-audit.log",
              "analysis/brush-collision/engine-analysis.json",
              "analysis/brush-collision/public-engine-lookups.json",
              "analysis/brush-collision/predicate-before.log",
              "analysis/brush-collision/hull-guard-before.log"]
    for name in inputs[:2]:
        data = (ROOT / name).read_bytes()
        pe = int.from_bytes(data[0x3c:0x40], "little")
        assert data[pe:pe+6] == b"PE\0\0\x4c\x01", name
    audit = json.loads((OUT / "integration-clang-audit.json").read_text())
    assert audit["standard"] == "C++20" and all(row["passed"] for row in audit["builds"])
    dll = next(row for row in audit["artifacts"] if row["path"] == inputs[0])
    assert dll["sha256"] == helper.digest(ROOT / inputs[0])
    report = {
        "capturedAt": time.time(), "scope": __doc__, "nativeTests": names,
        "dynamic": dynamic, "event": event, "collisionCounters": counters,
        "sources": {name: helper.digest(ROOT / name) for name in sorted(sources)},
        "inputs": {name: helper.digest(ROOT / name) for name in inputs},
        "unchangedGlSources": unchanged,
        "preservation": helper.preserve(OUT / "integration-preservation.json"),
        "mainDeployed": False, "dynamicTransactionsEnabled": False,
        "remaining": ["Per-instance Renderer and atomic joint publication",
                      "Authoritative stale-target cleanup and model-local sampling",
                      "Actual B/input/network/rendering/Touch-Use/multiplayer/FPS"],
        "passed": True,
    }
    (OUT / "integration-evidence.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"nativeTests": len(names), "dynamic": dynamic,
                      "sources": len(sources), "inputs": len(inputs),
                      "preservation": report["preservation"], "passed": True}))


if __name__ == "__main__":
    main()
