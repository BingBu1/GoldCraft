"""Capture integrated native brush lifecycle evidence without starting a server.

Run the build and Exercise-BrushLifecycle (normal and --production-smoke), then
Exercise-MiningProducer first. Externally running primary instances are observed
only; this verifier never stops them or replaces their changing runtime files.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "analysis/brush-server"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def latest(pattern, count):
    for path in sorted(ROOT.glob(pattern), reverse=True):
        report = json.loads(path.read_text(encoding="utf-8"))
        if report.get("passed") and len(report.get("checks", {})) == count:
            assert all(report["checks"].values()) and report["fixtureStopped"] and report["fixtureFilesRestored"]
            return path, report
    raise ValueError(f"No completed {count}-check report for {pattern}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-external-differences", type=Path,
                        help="Previously inspected external-change record; no differences are silently accepted")
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("surface_evidence", ROOT / "tools/Verify-SurfaceMesh.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    text, names, log = helper.tests(ROOT / "build/native-clang-x86-Release", 31)
    assert "goldcraft_map_edit_lifecycle" in names
    assert "32 repeated failures+same-frame continuation" in text
    assert "15 allocation failures" in text and "4096 no-op frames allocate0" in text
    runtime_path, runtime = latest("analysis/brush-server/runtime-*.json", 35)
    smoke_path, smoke = latest("analysis/brush-server/runtime-*.json", 7)
    producer_path, producer = latest("analysis/world-carving/producer-native-*.json", 22)
    reuse = runtime["edictReuse"]
    assert reuse["oldSlot"] == reuse["newSlot"] and reuse["oldSerial"] != reuse["newSerial"]
    for report in (runtime, smoke, producer):
        for name, expected in report["artifacts"].items():
            assert digest(ROOT / name) == expected, f"Runtime input changed: {name}"
    # Preserved historical baselines remain intact even while human-owned
    # instances are active. Differences require an explicit inspected record.
    preservation_path = OUT / "integration-preservation.json"
    try:
        helper.preserve(preservation_path)
    except AssertionError:
        if not preservation_path.is_file():
            raise
    preservation = json.loads(preservation_path.read_text())
    expected_changes = set()
    if args.expected_external_differences:
        record = json.loads(args.expected_external_differences.read_text())
        expected_changes = set(record["files"])
    assert set(preservation["changed"]) == expected_changes, "Unreviewed installation/runtime differences"
    preservation["unchangedBaseline"] = not expected_changes
    preservation["differenceReason"] = record["reason"] if expected_changes else "No differences"
    audit_path = OUT / "integration-clang-audit.json"
    audit = json.loads(audit_path.read_text())
    assert audit["standard"] == "C++20" and all(item["passed"] for item in audit["builds"])
    for item in audit["artifacts"]:
        assert digest(ROOT / item["path"]) == item["sha256"]
    previous = json.loads((ROOT / "analysis/brush-collision/integration-evidence.json").read_text())
    gl_sources = previous["unchangedGlSources"]
    assert all(digest(ROOT / name) == expected for name, expected in gl_sources.items())
    sources = set(previous["sources"])
    sources.update(("native/server/map_mining.cpp", "native/server/map_mining.h", "native/server/bridge.cpp",
                    "native/server/GoldCraft.targets", "native/server/map_edit_commit.hpp", "native/server/map_brush_fixture.inc",
                    "native/engine/map_physics.cpp", "native/engine/map_physics.h", "native/engine/map_visibility.cpp",
                    "native/engine/map_visibility.h", "native/engine/GoldCraftEngine.targets",
                    "native/include/goldcraft/host_map_sample_api.hpp", "tests/native/map_edit_lifecycle_tests.cpp",
                    "tools/Build-ReHLDS.ps1", "tools/Exercise-BrushLifecycle.py", "tools/Verify-BrushServer.py",
                    "tools/Exercise-MiningProducer.py", "tools/Exercise-MapMining.py", "tools/Exercise-MapMiningRounds.py",
                    "tools/Exercise-WorldCarving.py", "tools/Exercise-ZombieReAPI.py"))
    for name in ("external/ReHLDS/rehlds/engine/world.cpp", "external/ReHLDS/rehlds/engine/model.cpp",
                 "external/ReHLDS/rehlds/engine/pr_edict.cpp", "external/ReHLDS/rehlds/engine/pr_cmds.cpp",
                 "external/ReHLDS/rehlds/public/interface.h", "external/ReHLDS/rehlds/public/interface.cpp"):
        sources.add(name)
    inputs = [runtime_path, smoke_path, producer_path, log, audit_path,
              OUT / "integration-native-build-02.log", OUT / "integration-production-build.log",
              OUT / "integration-engine-build.log", OUT / "integration-clang-audit.log",
              OUT / "integration-native-build.log",
              ROOT / "build/native-clang-x86-Release/compile_commands.json",
              ROOT / "build/native-x86/Release/goldcraft_map_edit_lifecycle_tests.exe",
              ROOT / "build/rehlds/Release/swds.dll", ROOT / "build/regamedll/Release/mp.dll",
              ROOT / "build/regamedll-headless/Release/mp.dll", OUT / "checkpoint.json"]
    evidence = {
        "capturedAt": time.time(), "scope": __doc__, "passed": True, "nativeTests": names,
        "runtime": runtime_path.relative_to(ROOT).as_posix(), "runtimeChecks": len(runtime["checks"]),
        "productionSmoke": smoke_path.relative_to(ROOT).as_posix(), "productionChecks": len(smoke["checks"]),
        "staticProducer": producer_path.relative_to(ROOT).as_posix(), "producerChecks": len(producer["checks"]),
        "sameSlotReuse": reuse, "preservation": preservation,
        "sources": {name: digest(ROOT / name) for name in sorted(sources)},
        "inputs": {path.relative_to(ROOT).as_posix(): digest(path) for path in inputs},
        "unchangedGlSources": gl_sources, "mainDeployed": False, "dynamicProducerEnabled": False,
        "remaining": ["Renderer and Java candidate integration; atomic client transaction",
                      "Loaded-engine internal preparation failure and stale sample metadata have only isolated adapter evidence",
                      "Actual B input/Touch-Use/visuals/multiplayer/fixed-view FPS"]}
    (OUT / "integration-evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps({"nativeTests": len(names), "runtimeChecks": 35, "productionChecks": 7, "producerChecks": 22,
                      "sources": len(sources), "inputs": len(inputs),
                      "baselineUnchanged": preservation["unchangedBaseline"],
                      "reviewedExternalDifferences": len(expected_changes), "passed": True}))


if __name__ == "__main__":
    main()
