"""Capture same-snapshot BSP identity evidence and preserved installations.

Checks the actual executed native suite and supplied independent-server reports.
The client probe runs only the exact engine's entity-state publication, with
model/animation adapters. No real B networking, dynamic cutting, rendering or
game FPS is accepted here. Run the server exercises serially before this tool.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("brush", "producer", "delivery", "entity"):
        parser.add_argument("--" + name + "-report", type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("brush_evidence", ROOT / "tools/Verify-SurfaceMesh.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    text, names, log = helper.tests(ROOT / "build/native-clang-x86-Release", 26)
    rows = [json.loads(line) for line in text.splitlines() if line.startswith('{') and line.endswith('}')]
    codec = next(row for row in rows if row.get("brushIdentity"))
    client = next(row for row in rows if row.get("actualEngineBrushIdentity"))
    assert codec["passed"] and codec["frames"] == 4096 and codec["lookups"] == 1048576
    assert codec["allocations"] == 0 and codec["atomicFragments"] and codec["lossWrapReset"]
    assert client["passed"] and client["originalStatePublication"] and client["sameSlotSameModelReuse"]
    assert client["userFieldsPreserved"]
    inputs = {}

    def record(path):
        path = (ROOT / path).resolve()
        assert path.is_relative_to(ROOT), "Evidence must stay in the workspace"
        relative = path.relative_to(ROOT).as_posix()
        actual = helper.digest(path)
        assert relative not in inputs or inputs[relative] == actual, relative
        inputs[relative] = actual
        return relative

    reports = {}
    runtime = {}
    for role, count in (("brush", 23), ("producer", 22), ("delivery", 62), ("entity", 29)):
        path = getattr(args, role + "_report")
        report = json.loads((ROOT / path).read_text(encoding="utf-8"))
        assert report["passed"] and report["fixtureFilesRestored"] and report["fixtureStopped"], role
        assert len(report["checks"]) == count and all(report["checks"].values()), role
        assert not report.get("runtimeErrors"), role
        for artifact, digest in report["artifacts"].items():
            assert helper.digest(ROOT / artifact) == digest, artifact
            record(artifact)
        reports[role] = {"path": record(path), "checks": count}
        runtime[role] = report
    replay = json.loads(runtime["brush"]["clientPublication"]["stdout"])
    assert runtime["brush"]["clientPublication"]["returncode"] == 0
    assert replay["passed"] and replay["replayedServerBytes"] and replay["userFieldsPreserved"]
    assert runtime["producer"]["productionTunnelCells"] == 6
    assert any("gc_brush_identity fixture command" in name for name in runtime["entity"]["checks"])

    previous = json.loads((ROOT / "analysis/world-carving/producer-evidence.json").read_text())
    for path, digest in previous["unchangedGlSources"].items():
        assert helper.digest(ROOT / path) == digest, path
    audit = json.loads((ROOT / "analysis/goldcraft-tests/clang-build-audit.json").read_text())
    assert audit["standard"] == "C++20" and audit["optimization"] == "O3 / ThinLTO / AVX2 / precise FP"
    assert len(audit["builds"]) == 12 and all(row["passed"] for row in audit["builds"])
    assert sum(row["cppUnits"] for row in audit["builds"]) == 1696
    assert len(audit["artifacts"]) == 16 and all(row["x86"] for row in audit["artifacts"])
    for row in audit["artifacts"]:
        assert helper.digest(ROOT / row["path"]) == row["sha256"], row["path"]
    patches = json.loads((ROOT / "analysis/publication/source-patches.json").read_text())
    lock = json.loads((ROOT / "sources.lock.json").read_text())
    assert len(patches) == 10 and all(row["passed"] for row in patches)
    for row in patches:
        path = lock["sources"][row["component"]]["patch"]
        assert helper.digest(ROOT / path) == row["patchSha256"], path
        record(path)
    sources = [
        "CMakeLists.txt", "sources.lock.json", "native/include/goldcraft/brush_identity.hpp",
        "native/include/goldcraft/wire.hpp", "native/include/goldcraft/map_edits.hpp",
        "native/include/goldcraft/host_map_api.hpp", "native/common/wire.cpp",
        "native/client/plugin.cpp", "native/client/map_collision.cpp", "native/client/map_collision.hpp",
        "native/client/map_edit_transaction.cpp", "native/client/map_edit_transaction.hpp",
        "native/engine/map_physics.cpp", "native/engine/map_physics.h", "native/engine/map_delivery_test.cpp",
        "native/engine/map_visibility.cpp", "native/engine/GoldCraftEngine.targets",
        "native/server/bridge.cpp", "native/server/map_mining.cpp", "native/server/map_mining.h",
        "tests/native/brush_identity_tests.cpp", "tests/native/brush_engine_tests.cpp",
        "tools/Exercise-BrushIdentity.py", "tools/Exercise-MiningProducer.py",
        "tools/Exercise-EntityVisibility.py", "tools/Exercise-MapDelivery.py", "tools/Verify-BrushIdentity.py",
        "tools/Verify-SurfaceMesh.py", "tools/Build-Native.ps1", "tools/Build-ReHLDS.ps1",
        "external/ReHLDS/rehlds/engine/sv_main.cpp", "external/ReHLDS/rehlds/common/qlimits.h",
    ]
    for path in (
        log, "build/native-x86/Release/GoldCraft.dll", "build/regamedll/Release/mp.dll",
        "build/native-x86/Release/goldcraft_brush_identity_tests.exe",
        "build/native-x86/Release/goldcraft_brush_engine_tests.exe",
        "build/native-clang-x86-Release/compile_commands.json",
        "sandbox/cs-client-b/Half-Life/hw.dll", "sandbox/cs-client-b/Half-Life/cstrike/maps/cs_assault.bsp",
        "analysis/world-carving/brush-engine-analysis.json", "analysis/world-carving/brush-server-payload.bin",
        "analysis/world-carving/brush-native-build.log", "analysis/world-carving/brush-native-final.log",
        "analysis/world-carving/brush-engine-build.log", "analysis/world-carving/brush-engine-fixture.log",
        "analysis/world-carving/brush-clang-audit.log", "analysis/goldcraft-tests/clang-build-audit.json",
        "analysis/publication/source-patches.json",
    ):
        record(path)
    report = {"capturedAt": time.time(), "scope": __doc__, "nativeTests": names,
              "codec": codec, "client": client, "serverReplay": replay, "reports": reports,
              "sources": {path: helper.digest(ROOT / path) for path in sources}, "inputs": inputs,
              "unchangedGlSources": previous["unchangedGlSources"],
              "preservation": helper.preserve("brush-preservation.json"), "passed": True}
    (ROOT / "analysis/world-carving/brush-evidence.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"nativeTests": len(names), "reports": reports, "codec": codec,
                      "sources": len(sources), "inputs": len(inputs), "preservation": report["preservation"],
                      "unchangedGlSources": len(report["unchangedGlSources"]), "passed": True}))


if __name__ == "__main__":
    main()
