"""Build the local NeoForge port against the exact production runtime and managed Mods."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import tomllib
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED
from Modpack import safe

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build/epicfight-maid"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def arguments(path, values):
    path.write_text("\n".join('"' + str(v).replace("\\", "\\\\").replace('"', '\\"') + '"'
                              for v in values), encoding="utf-8")


def add_entry(archive, name, data):
    entry = ZipInfo(name, (1980, 1, 1, 0, 0, 0))
    entry.create_system = 3
    entry.external_attr = 0o100644 << 16
    archive.writestr(entry, data, compress_type=ZIP_DEFLATED, compresslevel=9)


def check_source(name, entry, mod_id):
    source = safe(ROOT / entry["path"], ROOT / "external")
    commit = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if commit != entry["commit"]:
        raise RuntimeError(f"{name} source revision differs from sources.lock.json")
    metadata_path = source / "src/main/resources/META-INF/neoforge.mods.toml"
    if not metadata_path.is_file() or (metadata_path.with_name("mods.toml")).exists():
        raise RuntimeError(f"Apply the pinned {name} adaptation with tools/Prepare-Sources.ps1 first")
    metadata = tomllib.loads(metadata_path.read_text(encoding="utf-8"))
    if [(mod["modId"], mod["version"]) for mod in metadata["mods"]] != [(mod_id, entry["version"])]:
        raise RuntimeError(f"{name} metadata differs from the locked addon identity")
    untracked = subprocess.check_output(["git", "-C", str(source), "ls-files", "--others", "--exclude-standard", "-z", "--", "src/main"])
    unknown = set(untracked.decode().rstrip("\0").split("\0")) - {"", *entry.get("newFiles", [])}
    if unknown:
        raise RuntimeError(f"Unlisted addon source/resource files: {name}: {sorted(unknown)}")
    return source


def main():
    lock = json.loads((ROOT / "sources.lock.json").read_text(encoding="utf-8-sig"))
    pins = lock["minecraft"]
    addon, geo = lock["sources"]["EpicFightMaid"], lock["sources"]["YsmGeoCompat"]
    source = check_source("EpicFightMaid", addon, "ef_tlm")
    geo_source = check_source("YsmGeoCompat", geo, "ysm_geo_compat")
    neo = pins["neoforge"]
    mc = pins["version"]
    runtime = json.loads((ROOT / "build/neoforge-production/runClient.json").read_text(encoding="utf-8"))
    if runtime["runtimeKind"] != "production" or runtime["loaderVersion"] != neo or runtime["minecraft"] != mc:
        raise RuntimeError("Prepare the matching production NeoForge runtime first")
    safe(BUILD, ROOT / "build").mkdir(parents=True, exist_ok=True)
    libraries = ROOT / f".tools/neoforge-runtime/{neo}/libraries"
    game = f"{mc}-{pins['neoform']}"
    classpath = [libraries / f"net/neoforged/neoforge/{neo}/neoforge-{neo}-client.jar",
                 libraries / f"net/minecraft/client/{game}/client-{game}-srg.jar",
                 libraries / f"net/neoforged/neoforge/{neo}/neoforge-{neo}-universal.jar",
                 *map(Path, runtime["classpath"])]
    mods = ROOT / f"sandbox/modpack-neoforge/GoldCraft-{mc}-NeoForge/mods"
    dependencies = [p for p in sorted(mods.glob("*.jar")) if not p.name.startswith(("goldcraft", "ef_tlm"))]
    nested = safe(BUILD / "dependencies", ROOT / "build")
    nested.mkdir(exist_ok=True)
    for dependency in list(dependencies):
        with ZipFile(dependency) as archive:
            if "META-INF/jarjar/metadata.json" not in archive.namelist():
                continue
            for entry in json.loads(archive.read("META-INF/jarjar/metadata.json"))["jars"]:
                data = archive.read(entry["path"])
                destination = safe(nested / (hashlib.sha256(data).hexdigest() + ".jar"), ROOT / "build")
                if not destination.exists():
                    destination.write_bytes(data)
                dependencies.append(destination)
    classpath.extend(dependencies)
    annotations = list((ROOT / ".tools/gradle-cache/caches/modules-2/files-2.1/org.jetbrains/annotations/24.1.0").glob("*/*.jar"))
    if len(annotations) != 1:
        raise RuntimeError("Build GoldCraft once to prepare JetBrains annotations 24.1.0")
    classpath.extend(annotations)
    # Compile-only optional extension; its original JAR is never deployed or bundled.
    optional_extension = source / "libs/Nightfall-Enhance.jar"
    original_extension = subprocess.check_output(["git", "-C", str(source), "show", addon["commit"] + ":libs/Nightfall-Enhance.jar"])
    if sha(optional_extension) != hashlib.sha256(original_extension).hexdigest():
        raise RuntimeError("Compile-only Nightfall extension differs from the pinned upstream input")
    classpath.append(optional_extension)
    classes = safe(BUILD / "classes", ROOT / "build")
    if classes.exists():
        resolved = classes.resolve()
        if resolved.parent != BUILD.resolve() or classes.is_symlink() or classes.is_junction():
            raise RuntimeError("Refusing to clean a redirected compiler output")
        shutil.rmtree(resolved)
    classes.mkdir(exist_ok=True)
    sources = sorted((source / "src/main/java").rglob("*.java")) + sorted((geo_source / "src/main/java").rglob("*.java"))
    argfile = safe(BUILD / "javac.args", ROOT / "build")
    arguments(argfile, ["--release", "21", "-encoding", "UTF-8", "-parameters", "-proc:none", "-Xmaxerrs", "500",
                        "-cp", os.pathsep.join(map(str, dict.fromkeys(classpath))), "-d", classes, *sources])
    compiler = ROOT / ".tools/java/jdk-21.0.12.1+1/bin/javac.exe"
    result = subprocess.run([str(compiler), "-J-Duser.language=en", "-J-Duser.country=US", "-J-Dfile.encoding=UTF-8", "@" + str(argfile)], cwd=ROOT, capture_output=True)
    log = safe(BUILD / "compile.log", ROOT / "build")
    log.write_bytes(result.stdout + result.stderr)
    if result.returncode:
        print(json.dumps({"outcome": "compile-failed", "log": str(log)}))
        return result.returncode
    output = safe(BUILD / f"ef_tlm-{mc}-neoforge-{addon['version']}.jar", ROOT / "build")
    geo_output = safe(BUILD / f"ysm_geo_compat-{mc}-neoforge-{geo['version']}.jar", ROOT / "build")
    pending = safe(output.with_suffix(".jar.tmp"), ROOT / "build")
    geo_pending = safe(geo_output.with_suffix(".jar.tmp"), ROOT / "build")
    with ZipFile(geo_pending, "w", ZIP_DEFLATED) as archive:
        for base, prefix in ((classes / "com", "com/"), (geo_source / "src/main/resources", "")):
            for file in sorted(base.rglob("*")):
                if file.is_file():
                    add_entry(archive, prefix + file.relative_to(base).as_posix(), file.read_bytes())
        add_entry(archive, "LICENSE", (geo_source / "LICENSE").read_bytes())
    with ZipFile(pending, "w", ZIP_DEFLATED) as archive:
        for base, prefix in ((classes / "net", "net/"), (source / "src/main/resources", "")):
            for file in sorted(base.rglob("*")):
                if file.is_file():
                    add_entry(archive, prefix + file.relative_to(base).as_posix(), file.read_bytes())
        nested_path = "META-INF/jarjar/" + geo_output.name
        add_entry(archive, nested_path, geo_pending.read_bytes())
        add_entry(archive, "META-INF/jarjar/metadata.json", json.dumps({"jars": [{
            "identifier": {"group": "com.ysmef", "artifact": "ysm_geo_compat"},
            "version": {"range": f"[{geo['version']}]", "artifactVersion": geo["version"]},
            "path": nested_path, "isObfuscated": False}]}))
        add_entry(archive, "META-INF/MANIFEST.MF", "Manifest-Version: 1.0\nImplementation-Title: EpicFight_TouhouLittleMaid\nImplementation-Vendor: XcS\nImplementation-Version: " + addon["version"] + "\n\n")
    os.replace(geo_pending, geo_output)
    os.replace(pending, output)
    report = {"output": str(output), "sha256": sha(output), "minecraft": mc, "neoforge": neo,
              "dependencies": [{"file": p.name, "sha256": sha(p)} for p in dependencies],
              "compileClasspath": list(map(str, dict.fromkeys(classpath))),
              "geoSha256": sha(geo_output),
              "compileOnly": {"name": "Nightfall-Enhance.jar", "sha256": sha(optional_extension)},
              "geoCommit": geo["commit"], "sourceCommit": addon["commit"]}
    safe(BUILD / "build.json", ROOT / "build").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"outcome": "built", "jar": str(output), "sha256": report["sha256"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
