"""Install the pinned production NeoForge runtime entirely inside the workspace.

Use the unmodified official installer/processors and FML production providers.
Loom's Yarn development launch remains available for source-level GameTests;
downloaded Mojang-named Mods and the remapped GoldCraft JAR use this runtime.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
from zipfile import ZipFile

from Modpack import safe, MINECRAFT_PIN

ROOT = Path(__file__).resolve().parent.parent
VERSION = MINECRAFT_PIN["neoforge"]
MINECRAFT = MINECRAFT_PIN["version"]
NEOFORM = MINECRAFT_PIN["neoform"]
INSTALLER_SHA256 = MINECRAFT_PIN["installerSha256"]
MINECRAFT_METADATA_SHA1 = MINECRAFT_PIN["metadataSha1"]
INSTALL = ROOT / ".tools/neoforge-runtime" / VERSION
OUTPUT = ROOT / "build/neoforge-production"
LOOM = ROOT / ".tools/gradle-cache/caches/fabric-loom"
MODULES = ROOT / ".tools/gradle-cache/caches/modules-2/files-2.1"
JAVA = ROOT / ".tools/java/jdk-21.0.12.1+1/bin/java.exe"


def guarded(path):
    return safe(Path(path), ROOT)


def digest(path, algorithm="sha256"):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, algorithm).hexdigest()


def write(path, data):
    path = guarded(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def write_json(path, data):
    write(path, (json.dumps(data, indent=2) + "\n").encode("utf-8"))


def artifact_path(coordinate):
    coordinate, _, extension = coordinate.partition("@")
    parts = coordinate.split(":")
    if len(parts) not in (3, 4) or any(not p or "/" in p or "\\" in p for p in parts):
        raise ValueError(f"Invalid Maven coordinate: {coordinate}")
    group, name, version, *classifier = parts
    suffix = "-" + classifier[0] if classifier else ""
    return f"{group.replace('.', '/')}/{name}/{version}/{name}-{version}{suffix}.{extension or 'jar'}"


def allowed(rules):
    """Mojang's ordered rules, for this Windows x64 runtime and no demo features."""
    if not rules:
        return True
    result = False
    for rule in rules:
        platform = rule.get("os", {})
        if platform.get("name", "windows") != "windows":
            continue
        if "arch" in platform and not re.fullmatch(platform["arch"], "amd64"):
            continue
        if "version" in platform and not re.search(platform["version"], "10.0"):
            continue
        if any(value is not False for value in rule.get("features", {}).values()):
            continue
        result = rule["action"] == "allow"
    return result


def client_libraries(vanilla, neo):
    # Inherited versions override group/artifact/classifier, not unrelated natives.
    libraries = {}
    for entry in [*vanilla["libraries"], *neo["libraries"]]:
        if not allowed(entry.get("rules")):
            continue
        parts = entry["name"].split("@")[0].split(":")
        key = (parts[0], parts[1], parts[3] if len(parts) > 3 else "")
        libraries[key] = entry
    return list(libraries.values())


def obtain(path, download, candidates=()):
    """Reuse verified workspace files; every network artifact has a pinned hash."""
    path = guarded(path)
    checksum = download["sha1"]
    if not re.fullmatch(r"[0-9a-f]{40}", checksum):
        raise ValueError("Artifact lacks a valid SHA-1")
    if path.is_file() and digest(path, "sha1") == checksum:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    for candidate in candidates:
        if candidate.is_file() and digest(candidate, "sha1") == checksum:
            shutil.copyfile(candidate, path)
            return
    temporary = guarded(path.with_name(path.name + ".download"))
    try:
        subprocess.run(["curl.exe", "--fail", "--location", "--silent", "--show-error",
                        "--retry", "2", "--connect-timeout", "20", "--max-time", "180",
                        "--output", str(temporary), download["url"]], check=True, cwd=INSTALL)
        if digest(temporary, "sha1") != checksum:
            raise RuntimeError(f"Artifact checksum mismatch: {path.name}")
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            guarded(temporary).unlink()


def library(entry):
    download = entry.get("downloads", {}).get("artifact")
    if not download or not download.get("url"):
        return  # Installer-generated patched/slim/extra artifacts.
    relative = download["path"]
    if relative != artifact_path(entry["name"]):
        raise ValueError(f"Unexpected Maven artifact path: {entry['name']}")
    parts = entry["name"].split("@")[0].split(":")
    cache = MODULES / parts[0] / parts[1] / parts[2]
    obtain(INSTALL / "libraries" / relative, download, cache.glob("*/" + Path(relative).name))


def installer_run(side, installer, resources):
    # LegacyInstaller's LocalSource.fromResource also reads sibling
    # classpath resources. Supply hash-verified Mojang mappings through that
    # documented source path, leaving the signed installer JAR unchanged.
    temp = guarded(INSTALL / "tmp")
    temp.mkdir(exist_ok=True)
    log = guarded(OUTPUT / f"install-{side}.log")
    environment = dict(os.environ, mcdir=str(INSTALL))
    environment.pop("MOD_CLASSES", None)
    arguments = [str(JAVA), f"-Djava.io.tmpdir={temp}", "-Djava.awt.headless=true",
                 "-cp", os.pathsep.join([str(resources), str(installer)]),
                 "net.minecraftforge.installer.SimpleInstaller", "--offline",
                 "--installClient" if side == "client" else "--installServer", str(INSTALL)]
    print(json.dumps({"phase": f"official {side} installer", "log": str(log)}), flush=True)
    with log.open("wb") as output:
        result = subprocess.run(arguments, cwd=INSTALL, env=environment, stdout=output,
                                stderr=subprocess.STDOUT, timeout=600,
                                creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode:
        raise RuntimeError(f"Official {side} installation failed; see {log}")


def substitute(value, replacements):
    for key, replacement in replacements.items():
        value = value.replace("${" + key + "}", replacement)
    if "${" in value:
        raise ValueError(f"Unresolved launcher substitution: {value}")
    return value


def manifests(neo, vanilla, selected):
    libraries = (INSTALL / "libraries").as_posix()
    base = {"loader": "neoforge", "loaderVersion": VERSION, "minecraft": MINECRAFT,
            "runtimeKind": "production", "mappings": "mojang", "main": neo["mainClass"],
            "environment": {}, "clearEnvironment": ["MOD_CLASSES", "CLASSPATH"]}
    replacements = {"library_directory": libraries, "classpath_separator": os.pathsep,
                    "version_name": neo["id"]}
    client_cp = [str(INSTALL / "libraries" / entry["downloads"]["artifact"]["path"])
                 for entry in selected]
    client_vm = [substitute(v, replacements) for v in neo["arguments"]["jvm"]]
    asset_root = LOOM / "assets"
    asset_id = vanilla["assetIndex"]["id"]
    obtain(asset_root / "indexes" / f"{asset_id}.json", vanilla["assetIndex"],
           [asset_root / "indexes" / f"{MINECRAFT}-{asset_id}.json"])
    client_args = [*neo["arguments"]["game"], "--version", neo["id"], "--versionType", "release",
                   "--assetsDir", str(asset_root), "--assetIndex", asset_id,
                   "--accessToken", "0", "--userType", "legacy"]
    client = {**base, "jvm": ["-Xmx3G", "-Dfile.encoding=UTF-8", *client_vm],
              "classpath": client_cp, "args": client_args,
              "workdir": str(ROOT / "sandbox/neoforge-cs-client-a")}
    # Preserve the official server launch arguments, changing only relative
    # library paths because each running instance has its own working directory.
    args_file = INSTALL / "libraries/net/neoforged/neoforge" / VERSION / "win_args.txt"
    arguments = shlex.split(args_file.read_text(encoding="utf-8"), comments=True)
    main_index = arguments.index(base["main"])
    server_vm = []
    for value in arguments[:main_index]:
        if value == "-DlibraryDirectory=libraries":
            value = "-DlibraryDirectory=" + libraries
        else:
            value = value.replace("libraries/", libraries + "/")
        server_vm.append(value)
    server = {**base, "jvm": ["-Xmx2G", "-Dfile.encoding=UTF-8", *server_vm], "classpath": [],
              "args": [*arguments[main_index + 1:], "nogui"],
              "workdir": str(ROOT / "sandbox/neoforge-server")}
    for name, manifest in [("runClient", client), ("runServer", server)]:
        write_json(OUTPUT / (name + ".json"), manifest)
    return client, server


def verify():
    index = json.loads((OUTPUT / "installation.json").read_text(encoding="utf-8"))
    if index["installerSha256"] != INSTALLER_SHA256 or index["neoForge"] != VERSION:
        raise RuntimeError("Production runtime version does not match this workspace")
    for record in index["files"]:
        file = guarded(ROOT / record["path"])
        if not file.is_file() or digest(file) != record["sha256"]:
            raise RuntimeError(f"Production runtime changed or missing: {record['path']}")
    print(json.dumps({"phase": "production runtime verified", "neoForge": VERSION,
                      "minecraft": MINECRAFT, "files": len(index["files"])}), flush=True)


def prepare():
    guarded(INSTALL).mkdir(parents=True, exist_ok=True)
    guarded(OUTPUT).mkdir(parents=True, exist_ok=True)
    metadata = INSTALL / f"versions/{MINECRAFT}/{MINECRAFT}.json"
    obtain(metadata, {"sha1": MINECRAFT_METADATA_SHA1,
        "url": f"https://piston-meta.mojang.com/v1/packages/{MINECRAFT_METADATA_SHA1}/{MINECRAFT}.json"},
        [LOOM / MINECRAFT / "minecraft-info.json"])
    vanilla = json.loads(metadata.read_text(encoding="utf-8"))
    if vanilla["id"] != MINECRAFT:
        raise RuntimeError("Pinned metadata targets a different Minecraft version")
    jars = list((MODULES / "net.neoforged/neoforge" / VERSION).glob(f"*/neoforge-{VERSION}-installer.jar"))
    if len(jars) != 1 or digest(jars[0]) != INSTALLER_SHA256:
        raise RuntimeError("Build the pinned NeoForge project first (official installer mismatch)")
    installer = guarded(INSTALL / jars[0].name)
    shutil.copyfile(jars[0], installer)
    with ZipFile(installer) as archive:
        profile = json.loads(archive.read("install_profile.json"))
        neo = json.loads(archive.read("version.json"))
    if profile["minecraft"] != MINECRAFT or neo["inheritsFrom"] != MINECRAFT:
        raise RuntimeError("Installer targets a different Minecraft version")
    selected = client_libraries(vanilla, neo)
    entries = {entry["name"]: entry for entry in [*profile["libraries"], *neo["libraries"], *selected]}
    print(json.dumps({"phase": "verified dependency preparation", "artifacts": len(entries)}), flush=True)
    for entry in entries.values():
        library(entry)
    for side in ("client", "server"):
        target = (INSTALL / f"versions/{MINECRAFT}/{MINECRAFT}.jar" if side == "client" else
                  INSTALL / f"libraries/net/minecraft/server/{MINECRAFT}/server-{MINECRAFT}.jar")
        obtain(target, vanilla["downloads"][side], [LOOM / MINECRAFT / f"minecraft-{side}.jar"])
    write(INSTALL / f"versions/{MINECRAFT}/{MINECRAFT}.json", metadata.read_bytes())
    resources = guarded(INSTALL / "installer-resources")
    for side in ("client", "server"):
        source = LOOM / MINECRAFT / "layered/working_dir/tmp-mojang/mojang" / f"{side}.txt"
        obtain(resources / f"maven/minecraft/{MINECRAFT}/{side}_mappings.txt",
               vanilla["downloads"][side + "_mappings"], [source])
    # This is a new isolated launcher profile, never the user's launcher registry.
    if not (INSTALL / "launcher_profiles.json").exists():
        write_json(INSTALL / "launcher_profiles.json", {"profiles": {}})
    for side in ("server", "client"):
        installer_run(side, installer, resources)
        patched = INSTALL / f"libraries/net/neoforged/neoforge/{VERSION}/neoforge-{VERSION}-{side}.jar"
        if not patched.is_file():
            raise RuntimeError(f"Installer returned without generating {side} classes")
    manifests(neo, vanilla, selected)
    files = [installer, *sorted((INSTALL / "libraries").rglob("*.jar")),
             OUTPUT / "runClient.json", OUTPUT / "runServer.json"]
    write_json(OUTPUT / "installation.json", {"minecraft": MINECRAFT, "neoForge": VERSION,
        "installerSha256": INSTALLER_SHA256, "installerSource": "LegacyInstaller " + MINECRAFT_PIN["installerVersion"],
        "installerSourcesSha256": MINECRAFT_PIN["installerSourcesSha256"],
        "files": [{"path": file.relative_to(ROOT).as_posix(), "sha256": digest(file)} for file in files]})
    verify()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", action="store_true", help="Check a prepared runtime without downloading or installing")
    if parser.parse_args().verify:
        verify()
    else:
        prepare()
