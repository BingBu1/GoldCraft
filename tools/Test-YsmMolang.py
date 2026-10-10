"""Regression checks for the local GEO Molang adapter; reads model assets from the pinned TLM JAR."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def argfile(path, values):
    path.write_text("\n".join('"' + str(value).replace("\\", "\\\\").replace('"', '\\"') + '"'
                              for value in values), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--source-root", type=Path, default=Path("external/YsmGeoCompat"))
    parser.add_argument("--test-root", type=Path, default=Path("tests/ysm-molang"))
    parser.add_argument("--output", type=Path, default=Path("analysis/ysm-molang-tests"))
    args = parser.parse_args()
    root = args.workspace.resolve()
    source = (root / args.source_root).resolve()
    tests = (root / args.test_root).resolve()
    output = (root / args.output).resolve()
    if not output.is_relative_to(root / "analysis"):
        raise RuntimeError("Test output must stay within the workspace analysis directory")
    for path in [output, *output.parents]:
        if path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction()):
            raise RuntimeError("Refusing redirected test output")
        if path == root:
            break
    output.mkdir(parents=True, exist_ok=True)
    classes = output / "classes"
    classes.mkdir(exist_ok=True)
    build_file = root / "build/epicfight-maid/build.json"
    build = json.loads(build_file.read_text(encoding="utf-8"))
    lock = json.loads((root / "sources.lock.json").read_text(encoding="utf-8-sig"))
    geo_version = lock["sources"]["YsmGeoCompat"]["version"]
    geo_jar = root / f"build/epicfight-maid/ysm_geo_compat-{build['minecraft']}-neoforge-{geo_version}.jar"
    if sha(geo_jar) != build["geoSha256"]:
        raise RuntimeError("GEO JAR differs from the recorded addon build; rebuild first")
    classpath = [*build["compileClasspath"], str(geo_jar)]
    tlm = next(Path(path) for path in classpath if Path(path).name.startswith("touhoulittlemaid-") and path.endswith(".jar"))
    tlm_pin = next(dependency["sha256"] for dependency in build["dependencies"] if dependency["file"] == tlm.name)
    if sha(tlm) != tlm_pin:
        raise RuntimeError("TLM JAR differs from the build's pinned dependency")
    runtime_classpath = [*classpath, *(path.replace("-srg.jar", "-extra.jar") for path in classpath
                                    if path.endswith("-srg.jar") and Path(path.replace("-srg.jar", "-extra.jar")).is_file())]
    relative_sources = ["com/ysmef/geomodel/ysm/script/Molang.java",
                        "com/ysmef/geomodel/model/runtime/BoneRotationState.java",
                        "com/ysmef/geomodel/model/runtime/YSMPlayerAnimator.java",
                        "com/ysmef/geomodel/model/runtime/YSMRuntimeModel.java"]
    sources = [source / "src/main/java" / path for path in relative_sources]
    test_sources = sorted(tests.rglob("*.java"))
    if len(test_sources) != 3:
        raise RuntimeError("Expected the three checked-in Molang regression probes")
    jdk = root / ".tools/java/jdk-21.0.12.1+1/bin"
    runs = []

    def run(name, executable, arguments):
        arguments_file = output / (name + ".args")
        argfile(arguments_file, arguments)
        result = subprocess.run([str(jdk / executable), "@" + str(arguments_file)], cwd=output,
                                capture_output=True, timeout=90)
        log = output / (name + ("-failed-" + str(time.time_ns()) if result.returncode else "") + ".log")
        log.write_bytes(result.stdout + result.stderr)
        runs.append({"name": name, "exitCode": result.returncode, "log": log.name})
        print((result.stdout + result.stderr).decode("utf-8", errors="replace"))
        return result.returncode

    code = run("compile", "javac.exe", ["--release", "21", "-encoding", "UTF-8", "-proc:none", "-cp",
                                       os.pathsep.join(classpath), "-d", classes, *sources, *test_sources])
    if not code:
        fixtures = output / "fixtures"
        for name, entrypoint, parameters in [
                ("fixtures", "LocalModelFixtures", [tlm, fixtures]),
                ("parser-state", "BoneRotationProbe", [fixtures]),
                ("animator", "AnimatorIntegrationProbe", [fixtures / "zhiban_hanfu.runtime.json"])]:
            code = run(name, "java.exe", ["-Dfile.encoding=UTF-8", "-cp",
                                         os.pathsep.join([str(classes), *runtime_classpath]),
                                         "com.ysmef.geomodel.model.runtime." + entrypoint, *parameters])
            if code:
                break
    report = {"buildSha256": sha(build_file), "geoSha256": sha(geo_jar), "tlmSha256": tlm_pin,
              "sources": [{"path": str(path.relative_to(root)), "sha256": sha(path)} for path in [*sources, *test_sources]],
              "runs": runs, "exitCode": code,
              "boundary": "Isolated JVM, exact model/compiler/animator methods, empty LoadingModList; no full FML or game rendering"}
    (output / "results.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
