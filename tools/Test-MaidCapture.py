"""Exercise maid capture against the pinned production Minecraft/Epic Fight APIs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def add_entry(archive, name, data):
    entry = ZipInfo(name, (1980, 1, 1, 0, 0, 0))
    entry.create_system = 3
    entry.external_attr = 0o100644 << 16
    archive.writestr(entry, data, compress_type=ZIP_DEFLATED, compresslevel=9)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("analysis/maid-capture"))
    args = parser.parse_args()
    output = (root / args.output).resolve()
    if not output.is_relative_to(root / "analysis"):
        raise RuntimeError("Test output must stay in workspace analysis")
    for path in [output, *output.parents]:
        if path.is_symlink() or path.is_junction():
            raise RuntimeError("Refusing redirected output")
        if path == root:
            break
    output.mkdir(parents=True, exist_ok=True)
    classes = output / "classes"
    classes.mkdir(exist_ok=True)
    build = json.loads((root / "build/epicfight-maid/build.json").read_text(encoding="utf-8"))
    classpath = list(build["compileClasspath"])
    for dependency in build["dependencies"]:
        path = next(Path(value) for value in classpath if Path(value).name == dependency["file"])
        if sha(path) != dependency["sha256"]:
            raise RuntimeError("Pinned dependency changed: " + path.name)
    classpath.extend(path.replace("-srg.jar", "-extra.jar") for path in list(classpath)
                     if path.endswith("-srg.jar") and Path(path.replace("-srg.jar", "-extra.jar")).is_file())
    asm = [root / ".tools/gradle/gradle-8.10.2/lib" / name
           for name in ("asm-9.7.jar", "asm-tree-9.7.jar", "asm-commons-9.7.jar")]
    classpath = [*map(str, asm), *classpath]
    jdk = root / ".tools/java/jdk-21.0.12.1+1/bin"
    sources = sorted((root / "tests/maid-capture").glob("*.java"))
    runs = []

    def run(name, executable, arguments):
        argfile = output / (name + ".args")
        argfile.write_text("\n".join('"' + str(value).replace("\\", "\\\\").replace('"', '\\"') + '"'
                                    for value in arguments), encoding="utf-8")
        result = subprocess.run([str(jdk / executable), "@" + str(argfile)], cwd=output,
                                capture_output=True, timeout=90)
        (output / (name + ".log")).write_bytes(result.stdout + result.stderr)
        runs.append({"name": name, "exitCode": result.returncode})
        if result.returncode:
            print((result.stdout + result.stderr).decode("utf-8", errors="replace"))
        else:
            print(f"PASS {name}")
        return result.returncode

    addon = root / "external/EpicFight_TouhouLittleMaid"
    geo = root / "external/YsmGeoCompat"
    patch = root / "patches/ysm-geo-neoforge.patch"
    patch_text = patch.read_text(encoding="utf-8")
    mesh_patch = patch_text.split(
        "diff --git a/src/main/java/com/ysmef/geomodel/model/YSMMesh.java b/src/main/java/com/ysmef/geomodel/model/YSMMesh.java\n", 1
    )[1].split("\ndiff --git ", 1)[0]
    if "+            super.drawPosed(poseStack, bufferSources.getBuffer(externalCaptureRenderType(renderType))," not in mesh_patch:
        raise AssertionError("The reproducible YSM GEO patch must triangulate the external capture path")
    addon_sources = sorted((addon / "src/main/java").rglob("*.java")) + sorted((geo / "src/main/java").rglob("*.java"))
    code = run("compile", "javac.exe", ["--release", "21", "-encoding", "UTF-8", "-parameters",
                                        "-proc:none", "-Xmaxerrs", "500", "-cp", os.pathsep.join(classpath),
                                        "-d", classes, *addon_sources, *sources])
    if not code:
        javap = subprocess.run([str(jdk / "javap.exe"), "-p", "-c", "-cp", str(classes),
                               "com.ysmef.geomodel.model.YSMMesh"], capture_output=True, timeout=30)
        (output / "bytecode.log").write_bytes(javap.stdout + javap.stderr)
        bytecode = (javap.stdout + javap.stderr).decode("utf-8", errors="replace")
        if javap.returncode or bytecode.count("externalCaptureRenderType") != 3 \
                or bytecode.count("EpicFightRenderTypes.makeTriangulated") != 1 \
                or bytecode.count("EpicFightRenderTypes.getTriangulated") != 1:
            raise AssertionError("Capture and CPU fallback must use cached triangulation; compute path remains unchanged")
        code = run("probe", "java.exe", ["-Dfile.encoding=UTF-8", "-cp",
                                         os.pathsep.join([str(classes), *classpath]),
                                         "MaidCaptureProbe", "3186", classes / "com/ysmef/geomodel/model/YSMMesh.class"])
    artifacts = {}
    if not code:
        lock = json.loads((root / "sources.lock.json").read_text(encoding="utf-8-sig"))
        addon_pin, geo_pin = lock["sources"]["EpicFightMaid"], lock["sources"]["YsmGeoCompat"]
        geo_output = output / f"ysm_geo_compat-{build['minecraft']}-neoforge-{geo_pin['version']}.jar"
        addon_output = output / f"ef_tlm-{build['minecraft']}-neoforge-{addon_pin['version']}.jar"
        with ZipFile(geo_output, "w", ZIP_DEFLATED) as archive:
            for base, prefix in ((classes / "com", "com/"), (geo / "src/main/resources", "")):
                for path in sorted(base.rglob("*")):
                    if path.is_file():
                        add_entry(archive, prefix + path.relative_to(base).as_posix(), path.read_bytes())
            add_entry(archive, "LICENSE", (geo / "LICENSE").read_bytes())
        nested = "META-INF/jarjar/" + geo_output.name
        with ZipFile(addon_output, "w", ZIP_DEFLATED) as archive:
            for base, prefix in ((classes / "net", "net/"), (addon / "src/main/resources", "")):
                for path in sorted(base.rglob("*")):
                    if path.is_file():
                        add_entry(archive, prefix + path.relative_to(base).as_posix(), path.read_bytes())
            add_entry(archive, nested, geo_output.read_bytes())
            add_entry(archive, "META-INF/jarjar/metadata.json", json.dumps({"jars": [{
                "identifier": {"group": "com.ysmef", "artifact": "ysm_geo_compat"},
                "version": {"range": f"[{geo_pin['version']}]", "artifactVersion": geo_pin["version"]},
                "path": nested, "isObfuscated": False}]}))
            add_entry(archive, "META-INF/MANIFEST.MF", "Manifest-Version: 1.0\nImplementation-Title: EpicFight_TouhouLittleMaid\nImplementation-Vendor: XcS\nImplementation-Version: " + addon_pin["version"] + "\n\n")
        artifacts = {str(geo_output.relative_to(root)): sha(geo_output),
                     str(addon_output.relative_to(root)): sha(addon_output)}
    (output / "results.json").write_text(json.dumps({"runs": runs, "exitCode": code,
        "sources": {str(path.relative_to(root)): sha(path) for path in [*addon_sources, *sources, patch]},
        "artifacts": artifacts,
        "boundary": "Pinned addon compile and cache bytecode executed with fixture layer/factory types; captured 3186-vertex topology; no registry bootstrap, real Minecraft RenderType execution or visible game acceptance"}, indent=2), encoding="utf-8")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
