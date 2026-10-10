"""Compare hidden GL fixtures and record exact client build/preservation evidence.

This does not start CS, measure gameplay FPS, or validate actual Renderer
shader/engine integration. Both candidates use the same driver-backed harness,
small scene shaders, exact-SDK adapters and the production HUD shader.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "analysis/gl-state"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True, help="Commit supplying the saved baseline backend")
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("evidence", ROOT / "tools/Verify-SurfaceMesh.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    reference = subprocess.check_output(["git", "rev-parse", args.reference], cwd=ROOT, text=True).strip()
    for name in ("render_backend.cpp", "render_backend.hpp"):
        original = subprocess.check_output(["git", "show", f"{reference}:native/client/{name}"], cwd=ROOT)
        saved = (OUT / "baseline" / name).read_bytes()
        assert saved.replace(b"\r\n", b"\n") == original.replace(b"\r\n", b"\n"), name

    text, native_tests, native_log = helper.tests(ROOT / "build/native-clang-x86-Release", 24)
    executed = [json.loads(line) for line in text.splitlines() if line.startswith('{"renderBackend')]
    assert len(executed) == 5 and all(row["passed"] for row in executed)
    samples = {"baseline": [], "candidate": []}
    logs = []
    drivers = []
    pixel_hashes = {executed[-1]["pixelHash"]}
    for repetition in range(3):
        for variant in (("baseline", "candidate") if repetition % 2 == 0 else ("candidate", "baseline")):
            executable = ROOT / "build/native-x86/Release" / (
                "goldcraft_render_baseline.exe" if variant == "baseline" else "goldcraft_render_backend_tests.exe")
            result = subprocess.run([str(executable)], cwd=ROOT, capture_output=True, text=True,
                                    encoding="utf-8", timeout=40)
            log = OUT / f"{variant}-{repetition}.jsonl"
            log.write_text(result.stdout + result.stderr, encoding="utf-8")
            logs.append(log.relative_to(ROOT).as_posix())
            assert result.returncode == 0, log
            records = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
            assert len(records) == 6 and records[-1]["passed"] and records[-1]["contexts"] == 2
            pixel_hashes.add(records[-1]["pixelHash"])
            drivers.append(records[0])
            samples[variant].append({row["case"]: row for row in records if row.get("renderBackendGL")})
    assert len(pixel_hashes) == 1, "Before/after pixel readbacks differ"
    assert all(driver == drivers[0] for driver in drivers)
    comparisons = {}
    for name in ("opaque", "gbuffer", "feedback", "hud"):
        rows = {variant: [run[name] for run in runs] for variant, runs in samples.items()}
        for candidate in rows["candidate"]:
            assert candidate["allocations"] == candidate["uniformLocations"] == candidate["uploads"] == 0
            assert candidate["stateQueries"] < rows["baseline"][0]["stateQueries"]
        comparisons[name] = {}
        for variant, cases in rows.items():
            assert all(row["iterations"] == 256 and row["passed"] for row in cases)
            counts = {key: cases[0][key] / 256 for key in (
                "allocations", "stateQueries", "uniformLocations", "uniformWrites", "textureBinds", "uploads")}
            assert all(all(row[key] / 256 == value for key, value in counts.items()) for row in cases)
            values = [row["submitCpuUs"] for row in cases]
            comparisons[name][variant] = {"perSubmission": counts, "submitCpuUs": values,
                                           "medianCpuUs": statistics.median(values)}

    previous = json.loads((ROOT / "analysis/world-carving/event-evidence.json").read_text(encoding="utf-8"))
    for path, digest in previous["unchangedRendererSources"].items():
        assert helper.digest(ROOT / path) == digest, path
    sources = ["CMakeLists.txt", "native/client/render_backend.cpp", "native/client/render_backend.hpp",
               "native/client/render_state.hpp", "tests/native/render_backend_tests.cpp",
               "tests/native/renderer_stub.py", "tools/Verify-RenderBackend.py", "tools/Build-Native.ps1",
               "tools/ClangToolchain.ps1", "sources.lock.json"]
    sources += ["native/client/shaders/goldcraft." + suffix + ".glsl" for suffix in ("vert", "frag", "geom")]
    sources += ["external/MetaHookSv-20261007/Plugins/Renderer/" + path for path in (
        "include/Interface/IMetaRenderer.h", "src/gl_common.h", "src/gl_shader.cpp", "src/gl_rmisc.cpp", "src/gl_rmain.cpp")]
    inputs = ["build/native-x86/Release/GoldCraft.dll", "build/native-x86/Release/goldcraft_render_backend_tests.exe",
              "build/native-x86/Release/goldcraft_render_baseline.exe",
              "build/native-clang-x86-Release/renderer_unused.inc",
              "build/native-clang-x86-Release/compile_commands.json", str(native_log.relative_to(ROOT)),
              "analysis/gl-state/native-final.log", "analysis/gl-state/clang-audit.log",
              "analysis/gl-state/baseline/render_backend.cpp", "analysis/gl-state/baseline/render_backend.hpp"] + logs
    for path in inputs[:3]:
        data = (ROOT / path).read_bytes()
        pe = int.from_bytes(data[0x3c:0x40], "little")
        assert data[pe:pe+6] == b"PE\0\0\x4c\x01", path
    report = {"capturedAt": time.time(), "scope": __doc__, "reference": reference, "nativeTests": native_tests,
              "driver": drivers[0], "readback": executed[-1], "comparison": comparisons,
              "sources": {p: helper.digest(ROOT / p) for p in sources},
              "inputs": {p: helper.digest(ROOT / p) for p in inputs},
              "unchangedRendererSources": previous["unchangedRendererSources"],
              "preservation": helper.preserve("gl-state-preservation.json"), "passed": True}
    (OUT / "evidence.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"nativeTests": len(native_tests), "readback": report["readback"],
                      "comparison": comparisons, "preservation": report["preservation"], "passed": True}))


if __name__ == "__main__":
    main()
