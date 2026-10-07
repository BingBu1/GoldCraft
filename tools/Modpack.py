"""One XMCL-managed Mod source, deployed to local A/B/server with real loader validation."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import time
import tomllib
from zipfile import ZipFile, BadZipFile

ROOT = Path(__file__).resolve().parent.parent
LOADER = os.environ.get("GOLDCRAFT_MOD_LOADER", "neoforge")
if LOADER not in ("neoforge", "fabric"):
    raise ValueError("GOLDCRAFT_MOD_LOADER must be neoforge or fabric")
IS_NEOFORGE = LOADER == "neoforge"
PACK = ROOT / ("sandbox/modpack-neoforge" if IS_NEOFORGE else "sandbox/modpack")
SOURCE = PACK / ("GoldCraft-1.21-NeoForge" if IS_NEOFORGE else "GoldCraft-1.21")
TARGETS = {
    "cs-client-a": ("client", ROOT / ("sandbox/neoforge-cs-client-a" if IS_NEOFORGE else "sandbox/minecraft-cs-client-a")),
    "cs-client-b": ("client", ROOT / ("sandbox/neoforge-cs-client-b" if IS_NEOFORGE else "sandbox/minecraft-cs-client-b")),
    "cs-server": ("server", ROOT / ("sandbox/neoforge-server" if IS_NEOFORGE else "sandbox/mc-server")),
}
JAVA = ROOT / ".tools/java/jdk-21.0.12.1+1/bin/java.exe"
JAVAC = JAVA.with_name("javac.exe")
PLATFORM = ({"minecraft": "1.21", "loader": "21.0.167", "kind": "neoforge", "fml": "4.0.23"} if IS_NEOFORGE
            else {"minecraft": "1.21", "loader": "0.16.14", "fabricApi": "0.102.0+1.21"})


class PackError(RuntimeError):
    pass


def safe(path: Path, base: Path = ROOT / "sandbox") -> Path:
    """Validate the final absolute destination and each existing ancestor before every write."""
    path = Path(os.path.abspath(path))
    if not path.is_relative_to(base.absolute()) or path == base.absolute():
        raise PackError(f"Destination is outside the sandbox: {path}")
    for parent in (path, *path.parents):
        try:
            info = parent.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise PackError(f"Reparse point is not allowed: {parent}")
    return path


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def atomic_bytes(path, content):
    path = safe(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = safe(path.with_name(path.name + f".{os.getpid()}-{time.time_ns()}.goldcraft-tmp"))
    try:
        with temporary.open("xb") as stream:
            stream.write(content)
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            safe(temporary).unlink()


def write_json(path, value):
    atomic_bytes(path, (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))


def cached_jar(group, artifact, version):
    folder = ROOT / ".tools/gradle-cache/caches/modules-2/files-2.1" / group / artifact / version
    matches = list(folder.glob(f"*/{artifact}-{version}.jar"))
    if len(matches) != 1:
        raise PackError(f"Build dependencies first; expected one {artifact}-{version}.jar")
    return matches[0]


def core_jars():
    if IS_NEOFORGE:
        return {"goldcraft": ROOT / "neoforge/build/libs/goldcraft-neoforge-0.1.0-dev.jar"}
    return {
        "goldcraft": ROOT / "fabric/build/libs/goldcraft-0.1.0-dev.jar",
        "fabric-api": cached_jar("net.fabricmc.fabric-api", "fabric-api", PLATFORM["fabricApi"]),
    }


def initialize():
    safe(SOURCE).mkdir(parents=True, exist_ok=True)
    profile = SOURCE / "instance.json"
    if not profile.exists():
        write_json(profile, {
            "name": f"GoldCraft 1.21 {LOADER} · Mod 管理", "path": str(SOURCE),
            "description": "在此管理 Mod；使用工作区的同步并启动入口运行 CS/MC 联动。",
            "runtime": {"minecraft": PLATFORM["minecraft"], "fabricLoader": "" if IS_NEOFORGE else PLATFORM["loader"],
                        "forge": "", "neoForged": PLATFORM["loader"] if IS_NEOFORGE else "",
                        "quiltLoader": "", "optifine": "", "labyMod": ""},
            "version": "", "java": str(JAVA), "maxMemory": 3072, "assignMemory": True,
            "showLog": True, "hideLauncher": False, "creationDate": int(time.time() * 1000),
        })
    for file in core_jars().values():
        destination = safe(SOURCE / "mods" / file.name)
        if not destination.exists():
            atomic_bytes(destination, file.read_bytes())
        elif sha(destination) != sha(file):
            raise PackError(f"Core build changed: {file.name}; run refresh-core with all MC processes stopped")
    rules = PACK / "rules.json"
    if not rules.exists():
        write_json(rules, {"format": 1, "sideOverrides": {}, "sharedConfigs": []})


def scan_neoforge_jar(data, label, depth=0):
    if depth > 12:
        raise PackError(f"Nested jars are too deep: {label}")
    try:
        with ZipFile(io.BytesIO(data)) as jar:
            entry = jar.getinfo("META-INF/neoforge.mods.toml")
            if entry.file_size > 2 * 1024 * 1024:
                raise PackError(f"Oversized NeoForge metadata: {label}")
            metadata = tomllib.loads(jar.read(entry).decode("utf-8"))
            mods = metadata.get("mods", [])
            if not mods:
                raise PackError(f"No NeoForge mods declared: {label}")
            ids = [mod.get("modId", "") for mod in mods]
            if any(not re.fullmatch(r"[a-z][a-z0-9_]{1,63}", value) for value in ids) or len(set(ids)) != len(ids):
                raise PackError(f"Invalid or duplicate NeoForge mod ids: {label}")
            version = mods[0].get("version", "1")
            if not isinstance(version, str):
                raise PackError(f"Invalid NeoForge version: {label}")
            if "${file.jarVersion}" in version:
                manifest = jar.read("META-INF/MANIFEST.MF").decode("utf-8").replace("\r\n ", "")
                match = re.search(r"(?m)^Implementation-Version: (.+?)\r?$", manifest)
                version = version.replace("${file.jarVersion}", match.group(1) if match else "0.0NONE")
            # JarJar selects shared nested versions; its real selector validates
            # those below. A dependency's side must never classify this outer jar.
            if "META-INF/jarjar/metadata.json" in jar.namelist():
                for child in json.loads(jar.read("META-INF/jarjar/metadata.json")).get("jars", []):
                    relative = child.get("path", "")
                    if not relative or ".." in Path(relative).parts or Path(relative).is_absolute():
                        raise PackError(f"Invalid nested jar path: {label}")
                    if jar.getinfo(relative).file_size > 128 * 1024 * 1024:
                        raise PackError(f"Oversized nested jar: {label}/{relative}")
            return {"path": label, "metadata": {"id": ids[0], "ids": ids, "version": version,
                    "environment": "*", "neoforge": metadata}, "children": []}
    except (BadZipFile, KeyError, UnicodeError, tomllib.TOMLDecodeError, json.JSONDecodeError) as error:
        raise PackError(f"Not a complete NeoForge mod: {label}. Install the Minecraft 1.21 NeoForge edition. {error}") from error


def scan_jar(data, label, depth=0):
    if IS_NEOFORGE:
        return scan_neoforge_jar(data, label, depth)
    if depth > 12:
        raise PackError(f"Nested jars are too deep: {label}")
    try:
        with ZipFile(io.BytesIO(data)) as jar:
            metadata_entry = jar.getinfo("fabric.mod.json")
            if metadata_entry.file_size > 2 * 1024 * 1024:
                raise PackError(f"Oversized Fabric metadata: {label}")
            metadata = json.loads(jar.read(metadata_entry))
            mod_id = metadata.get("id", "")
            if not re.fullmatch(r"[a-z][a-z0-9_-]{1,63}", mod_id) or not isinstance(metadata.get("version"), str):
                raise PackError(f"Invalid Fabric id/version: {label}")
            children = []
            for item in metadata.get("jars", []):
                entry = jar.getinfo(item["file"])
                if entry.file_size > 128 * 1024 * 1024:
                    raise PackError(f"Oversized nested jar: {label}/{entry.filename}")
                children.append(scan_jar(jar.read(entry), label + "!/" + entry.filename, depth + 1))
            return {"path": label, "metadata": metadata, "children": children}
    except (BadZipFile, KeyError, json.JSONDecodeError) as error:
        raise PackError(f"Not a complete Fabric mod: {label}: {error}") from error


def side_assignment(node, overrides):
    metadata = node["metadata"]
    if IS_NEOFORGE:
        # NeoForge dependency.side restricts that dependency, not the owning Mod.
        # There is no Fabric-style install environment in neoforge.mods.toml.
        values = {overrides[i] for i in metadata.get("ids", [metadata["id"]]) if i in overrides}
        if len(values) > 1:
            raise PackError(f"Conflicting side rules for mods in the same jar: {metadata['ids']}")
        choice = next(iter(values), "both")
        choices = {"both": ["client", "server"], "client": ["client"], "server": ["server"], "disabled": []}
        if choice not in choices:
            raise PackError(f"Invalid NeoForge side override: {choice}")
        return choices[choice]
    declared = metadata.get("environment", "*")
    if declared not in ("*", "client", "server"):
        raise PackError(f"Invalid environment for {metadata['id']}: {declared}")
    allowed = {"client", "server"} if declared == "*" else {declared}
    override = overrides.get(metadata["id"])
    if override is None:
        return sorted(allowed)
    choices = {"both": {"client", "server"}, "client": {"client"}, "server": {"server"}, "disabled": set()}
    if override not in choices or not choices[override].issubset(allowed):
        raise PackError(f"Side override conflicts with {metadata['id']}'s Fabric environment")
    return sorted(choices[override])


def snapshot(source=SOURCE, rules_path=PACK / "rules.json", refresh_core=False):
    source = safe(source)
    profile = read_json(source / "instance.json")
    versions = profile.get("runtime", {})
    loader_key = "neoForged" if IS_NEOFORGE else "fabricLoader"
    if versions.get("minecraft") != PLATFORM["minecraft"] or versions.get(loader_key) != PLATFORM["loader"]:
        raise PackError(f"GoldCraft requires Minecraft 1.21 and {LOADER} {PLATFORM['loader']}; keep other versions in other instances")
    if any(versions.get(key) for key in ("forge", "fabricLoader" if IS_NEOFORGE else "neoForged", "quiltLoader", "optifine")):
        raise PackError(f"The GoldCraft profile must use {LOADER} without an additional loader")
    rules = read_json(rules_path)
    if rules.get("format") != 1:
        raise PackError("Unsupported modpack rule format")
    overrides = rules.get("sideOverrides", {})
    if not isinstance(overrides, dict):
        raise PackError("sideOverrides must be an object keyed by mod id")
    jars, ids, core_changes = [], {}, []
    cores = core_jars()
    for path in sorted((source / "mods").glob("*.jar"), key=lambda p: p.name.lower()):
        safe(path)
        if path.stat().st_size > 256 * 1024 * 1024:
            raise PackError(f"Oversized mod jar: {path.name}")
        content = path.read_bytes()
        node = scan_jar(content, str(path))
        mod_id = node["metadata"]["id"]
        for provided_id in node["metadata"].get("ids", [mod_id]):
            if provided_id in ids:
                raise PackError(f"Duplicate top-level mod id {provided_id}: {ids[provided_id]} and {path.name}")
            ids[provided_id] = path.name
        if refresh_core and mod_id in cores:
            built = cores[mod_id]
            candidate = built.read_bytes()
            before = hashlib.sha256(content).hexdigest()
            after = hashlib.sha256(candidate).hexdigest()
            if before != after:
                # Validate the candidate in memory before stopping a game or
                # replacing the XMCL source. The commit uses the normal rollback.
                node = scan_jar(candidate, str(built))
                if node["metadata"]["id"] != mod_id:
                    raise PackError(f"Unexpected core build id: {built.name}")
                core_changes.append({"target": "mod-source", "id": mod_id, "path": "mods/" + path.name,
                                     "destination": str(path), "before": before, "after": after, "source": str(built)})
                content = candidate
        node.update(name=path.name, id=mod_id, version=node["metadata"]["version"],
                    sha256=hashlib.sha256(content).hexdigest(), sides=side_assignment(node, overrides))
        jars.append(node)
    for mod_id, artifact in cores.items():
        selected = next((n for n in jars if n["id"] == mod_id), None)
        if not selected or set(selected["sides"]) != {"client", "server"} or selected["sha256"] != sha(artifact):
            raise PackError(f"Keep the matching {mod_id} core jar supplied by this workspace build")
    configs = []
    for relative in rules.get("sharedConfigs", []):
        item = Path(relative)
        if item.is_absolute() or ".." in item.parts or not item.parts or item.parts[0] != "config":
            raise PackError("Shared configuration paths must be individual files below config/")
        if item.name.lower() in ("fabric_loader_dependencies.json", "fml.toml") or item.name.lower().startswith("goldcraft"):
            raise PackError("Loader overrides and local bridge configuration are not shared mod settings")
        file = safe(source / item)
        if not file.is_file():
            raise PackError(f"Shared configuration is missing: {relative}")
        configs.append({"name": item.as_posix(), "path": str(file), "sha256": sha(file)})
    contract = {"platform": PLATFORM, "jars": [{k: n[k] for k in ("name", "id", "version", "sha256", "sides")} for n in jars],
                "configs": [{k: n[k] for k in ("name", "sha256")} for n in configs], "coreFromDevelopmentClasspath": True}
    fingerprint = hashlib.sha256(json.dumps(contract, sort_keys=True).encode()).hexdigest()
    return {**contract, "fingerprint": fingerprint, "nodes": jars, "configNodes": configs, "coreChanges": core_changes,
            "unusedOverrides": sorted(set(overrides) - ids.keys())}


def validate_neoforge(pack):
    source = ROOT / "tools/java/NeoForgePackValidator.java"
    runtime = read_json(ROOT / "build/neoforge-runtime/runServer.json")
    paths = sorted({str(Path(p)) for p in runtime["classpath"] if Path(p).suffix == ".jar"
                    and "modules-2" in Path(p).parts and "-natives-" not in Path(p).name})
    loader = cached_jar("net.neoforged.fancymodloader", "loader", PLATFORM["fml"])
    if not any(Path(p).name == loader.name for p in runtime["classpath"]):
        raise PackError("The NeoForge runtime manifest does not use the pinned FML loader")
    # Loom remaps FML's ObjectHolder runtime support for development names.
    # The metadata tool deliberately reads the matching original Maven artifact.
    paths.append(str(loader))
    key = hashlib.sha256((sha(source) + json.dumps([(Path(p).name, sha(p)) for p in paths])).encode()).hexdigest()
    folder = safe(PACK / "validation" / pack["fingerprint"] / key[:16])
    folder.mkdir(parents=True, exist_ok=True)
    result_path = folder / "result.json"
    if result_path.exists():
        cached = read_json(result_path)
        if cached.get("validatorKey") == key:
            if not cached["valid"]:
                raise PackError(f"NeoForge dependency validation failed; see {result_path}")
            return cached
    build = safe(PACK / "validator-classes")
    build.mkdir(parents=True, exist_ok=True)
    classpath = os.pathsep.join(paths)
    def run_java(executable, arguments, name):
        argument_file = safe(folder / (name + ".args"))
        quoted = '\n'.join('"' + str(a).replace('\\', '\\\\').replace('"', '\\"') + '"' for a in arguments)
        atomic_bytes(argument_file, quoted.encode("utf-8"))
        return subprocess.run([str(executable), "@" + str(argument_file)], cwd=folder,
                              capture_output=True, text=True, encoding="utf-8", errors="replace")
    compiled_key = build / "key.txt"
    if not compiled_key.exists() or compiled_key.read_text() != key:
        result = run_java(JAVAC, ["-encoding", "UTF-8", "-cp", classpath, "-d", build, source], "compile")
        if result.returncode:
            raise PackError("NeoForge validator compilation failed:\n" + result.stderr)
        atomic_bytes(compiled_key, key.encode())
    request = folder / "request.json"
    write_json(request, {"workspace": str(ROOT), "minecraft": PLATFORM["minecraft"], "neoforge": PLATFORM["loader"],
                         "fml": PLATFORM["fml"], "jars": pack["nodes"]})
    result = run_java(JAVA, ["--add-opens=java.base/java.lang.invoke=ALL-UNNAMED",
        "--add-exports=java.base/sun.security.util=ALL-UNNAMED", "-Dfile.encoding=UTF-8",
        "-cp", str(build) + os.pathsep + classpath, "dev.goldcraft.tools.NeoForgePackValidator", request, result_path], "validate")
    atomic_bytes(folder / "validator.log", (result.stdout + result.stderr).encode("utf-8"))
    if not result_path.exists():
        raise PackError(f"NeoForge validator did not return a result; see {folder / 'validator.log'}")
    validated = read_json(result_path)
    validated["validatorKey"] = key
    write_json(result_path, validated)
    if result.returncode or not validated["valid"]:
        reasons = [f"{side}: {validated[side].get('error', 'failed')}" for side in ("client", "server") if not validated[side]["valid"]]
        raise PackError("NeoForge dependency validation failed:\n" + "\n".join(reasons))
    return validated


def validate(pack):
    if IS_NEOFORGE:
        return validate_neoforge(pack)
    source = ROOT / "tools/java/GoldCraftPackValidator.java"
    loader = cached_jar("net.fabricmc", "fabric-loader", PLATFORM["loader"])
    runtime = read_json(ROOT / "build/fabric-runtime/runClient.json")
    gson = [p for p in runtime["classpath"] if Path(p).name.startswith("gson-") and Path(p).suffix == ".jar"]
    if len(gson) != 1:
        raise PackError("Cannot identify the pinned Minecraft Gson dependency")
    classpath = os.pathsep.join((str(loader), gson[0]))
    build = safe(PACK / "validator-classes")
    build.mkdir(parents=True, exist_ok=True)
    key = hashlib.sha256((sha(source) + sha(loader) + sha(gson[0])).encode()).hexdigest()
    folder = safe(PACK / "validation" / pack["fingerprint"] / key[:16])
    folder.mkdir(parents=True, exist_ok=True)
    result_path = folder / "result.json"
    if result_path.exists():
        cached = read_json(result_path)
        if cached.get("validatorKey") == key:
            if not cached["valid"]:
                raise PackError(f"Fabric dependency validation failed; see {result_path}")
            return cached
    compiled_key = build / "key.txt"
    if not compiled_key.exists() or compiled_key.read_text() != key:
        compile_result = subprocess.run([str(JAVAC), "-encoding", "UTF-8", "-cp", classpath, "-d", str(build), str(source)],
                                        cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if compile_result.returncode:
            raise PackError("Fabric validator compilation failed:\n" + compile_result.stderr)
        atomic_bytes(compiled_key, key.encode())
    config = safe(folder / "empty-config")
    config.mkdir(exist_ok=True)
    request = folder / "request.json"
    write_json(request, {"workspace": str(ROOT), "minecraft": PLATFORM["minecraft"], "loader": PLATFORM["loader"],
                         "validationConfig": str(config), "jars": pack["nodes"]})
    result = subprocess.run([str(JAVA), "-Dfile.encoding=UTF-8", "-cp", str(build) + os.pathsep + classpath,
                             "dev.goldcraft.tools.GoldCraftPackValidator", str(request), str(result_path)],
                            cwd=folder, capture_output=True, text=True, encoding="utf-8", errors="replace")
    atomic_bytes(folder / "validator.log", (result.stdout + result.stderr).encode("utf-8"))
    if not result_path.exists():
        raise PackError(f"Fabric validator failed to return a result; see {folder / 'validator.log'}")
    validated = read_json(result_path)
    validated["validatorKey"] = key
    write_json(result_path, validated)
    if result.returncode or not validated["valid"]:
        reasons = [f"{side}: {validated[side].get('error', 'failed')}" for side in ("client", "server") if not validated[side]["valid"]]
        raise PackError("Fabric dependency validation failed:\n" + "\n".join(reasons))
    return validated


def runtime_uses_loader(command_line):
    """Exclude a positively identified other loader/build, and block unknown Java consumers."""
    expanded = command_line or ""
    # The managed launchers use Java @argument files. Inspect only workspace
    # argument files and never print their contents (they may contain accounts).
    for quoted, plain in re.findall(r'@(?:"([^"\r\n]+)"|([^\s"\r\n]+))', expanded):
        path = Path(quoted or plain)
        if path.is_file() and path.resolve().is_relative_to(ROOT.resolve()) and path.stat().st_size < 1024 * 1024:
            expanded += " " + path.read_text(encoding="utf-8-sig", errors="replace")
    normalized = expanded.replace("\\\\", "/").replace("\\", "/").lower()
    root = ROOT.as_posix().lower()
    current = f"{root}/{LOADER}/"
    other_loader = "fabric" if IS_NEOFORGE else "neoforge"
    other = f"{root}/{other_loader}/"
    if current in normalized or f"{root}/build/{LOADER}-runtime/" in normalized:
        return True
    if other in normalized or f"{root}/build/{other_loader}-runtime/" in normalized:
        return False
    if re.search(r'org\.gradle\.(?:launcher|wrapper)\.', normalized):
        return False
    return True


def process_inventory():
    # Command lines stay in memory for classification; return only PID/name.
    needle = str(ROOT).replace("'", "''")
    command = f"$p=@(Get-CimInstance Win32_Process | Where-Object {{ $_.Name -match '^java(w)?\\.exe$' -and ($_.ExecutablePath -like '{needle}\\*' -or $_.CommandLine -like '*{needle}*') }} | Select-Object ProcessId,Name,CommandLine); ConvertTo-Json -InputObject $p -Compress"
    result = subprocess.run(["powershell.exe", "-NoProfile", "-Command", command], capture_output=True,
                            text=True, encoding="utf-8", errors="replace")
    if result.returncode:
        raise PackError("Cannot verify that Minecraft processes have stopped")
    return [{key: item[key] for key in ("ProcessId", "Name")}
            for item in json.loads(result.stdout or "[]") if runtime_uses_loader(item.get("CommandLine"))]


def deployment_plan(pack, targets=TARGETS, state_path=PACK / "deployed.json"):
    previous = read_json(state_path) if state_path.exists() else {"targets": {}}
    plan = {"fingerprint": pack["fingerprint"], "previousFingerprint": previous.get("fingerprint"), "targets": {}, "changes": []}
    for name, (side, directory) in targets.items():
        directory = safe(directory)
        files = {"mods/" + n["name"]: {"source": n["path"], "sha256": n["sha256"]} for n in pack["nodes"]
                 if side in n["sides"] and n["id"] not in ("goldcraft", "fabric-api")}
        files.update({n["name"]: {"source": n["path"], "sha256": n["sha256"]} for n in pack["configNodes"]})
        old = previous.get("targets", {}).get(name, {}).get("files", {})
        if (directory / "config/fabric_loader_dependencies.json").exists():
            raise PackError(f"{name} has a local Loader dependency override; align it before synchronization")
        for existing in (directory / "mods").glob("*.jar"):
            relative = existing.relative_to(directory).as_posix()
            if relative not in files and relative not in old:
                raise PackError(f"Unmanaged mod in {name}: {existing.name}; manage it through the source instance first")
        for relative in sorted(set(old) | set(files)):
            relative_path = Path(relative)
            if relative_path.is_absolute() or ".." in relative_path.parts or relative_path.parts[0] not in ("mods", "config"):
                raise PackError(f"Invalid managed relative path: {relative}")
            destination = safe(directory / relative)
            current = sha(destination) if destination.is_file() else None
            wanted = files.get(relative, {}).get("sha256")
            known = old.get(relative, {}).get("sha256")
            if current is not None and current not in (known, wanted):
                raise PackError(f"Locally changed file would be overwritten: {name}/{relative}")
            if current != wanted:
                plan["changes"].append({"target": name, "path": relative, "destination": str(destination),
                                        "before": current, "after": wanted, "source": files.get(relative, {}).get("source")})
        plan["targets"][name] = {"side": side, "directory": str(directory), "files": files}
    known_core = {n["id"]: n["sha256"] for n in previous.get("contract", {}).get("jars", [])}
    for change in pack.get("coreChanges", []):
        if known_core.get(change["id"]) != change["before"]:
            raise PackError(f"Core source {change['id']} does not match its last managed build; left unchanged")
        plan["changes"].append(change)
    plan["restartRequired"] = bool(plan["changes"] or plan["previousFingerprint"] != pack["fingerprint"])
    return plan


def apply(pack, plan, state_path=PACK / "deployed.json", check_processes=True):
    if not plan["restartRequired"]:
        return {"outcome": "unchanged", "fingerprint": pack["fingerprint"], "changedFiles": 0}
    running = process_inventory() if check_processes else []
    if running:
        raise PackError("Mod changes require stopped Minecraft processes; use Sync-Modpack.ps1 -Restart. Running PIDs: " +
                        ", ".join(str(p["ProcessId"]) for p in running))
    transaction = safe(PACK / "backups" / f"{time.time_ns()}-{pack['fingerprint'][:12]}")
    transaction.mkdir(parents=True)
    staged = {}
    for index, change in enumerate(plan["changes"]):
        path = safe(Path(change["destination"]))
        current = sha(path) if path.is_file() else None
        if current != change["before"]:
            raise PackError(f"Destination changed during planning: {change['target']}/{change['path']}")
        if current is not None:
            backup = safe(transaction / "previous" / change["target"] / change["path"])
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, backup)
        if change["after"]:
            content = Path(change["source"]).read_bytes()
            if hashlib.sha256(content).hexdigest() != change["after"]:
                raise PackError("A source mod changed while synchronizing; retry after its download completes")
            staged[index] = content
    completed = []
    try:
        for index, change in enumerate(plan["changes"]):
            destination = safe(Path(change["destination"]))
            current = sha(destination) if destination.is_file() else None
            if current != change["before"]:
                raise PackError(f"Destination changed before commit: {change['target']}/{change['path']}")
            if change["after"]:
                atomic_bytes(destination, staged[index])
                completed.append(change)
                if sha(destination) != change["after"]:
                    raise PackError(f"Copy verification failed: {destination}")
            else:
                destination.unlink()
                completed.append(change)
        write_json(transaction / "result.json", {"outcome": "copied", "fingerprint": pack["fingerprint"], "changes": plan["changes"]})
        write_json(state_path, {"format": 1, "fingerprint": pack["fingerprint"], "appliedAt": time.time(),
                                "targets": plan["targets"], "contract": {k: pack[k] for k in ("platform", "jars", "configs")}})
    except Exception:
        for change in reversed(completed):
            destination = safe(Path(change["destination"]))
            if change["before"]:
                atomic_bytes(destination, (transaction / "previous" / change["target"] / change["path"]).read_bytes())
            elif destination.exists():
                destination.unlink()
        raise
    return {"outcome": "applied", "fingerprint": pack["fingerprint"], "changedFiles": len(plan["changes"]), "backup": str(transaction)}


@contextmanager
def pack_lock():
    import msvcrt
    file = safe(PACK / "sync.lock")
    file.parent.mkdir(parents=True, exist_ok=True)
    with file.open("a+b") as stream:
        if stream.tell() == 0:
            stream.write(b"0")
            stream.flush()
        deadline = time.monotonic() + 30
        while True:
            try:
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise PackError("Another mod synchronization is still running")
                time.sleep(.1)
        try:
            yield
        finally:
            stream.seek(0)
            msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("init", "plan", "sync", "refresh-core"))
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--refresh-core", action="store_true", help="Validate the latest workspace core build and commit it only during stopped-runtime synchronization")
    args = parser.parse_args()
    if args.action == "init":
        initialize()
    pack = snapshot(refresh_core=args.refresh_core or args.action == "refresh-core")
    validated = validate(pack)
    plan = deployment_plan(pack)
    write_json(PACK / "plan.json", plan)
    summary = {"source": str(SOURCE), "minecraft": PLATFORM["minecraft"], "loader": PLATFORM["loader"],
               "mods": [{k: n[k] for k in ("id", "version", "sides")} for n in pack["nodes"]],
               "resolvedClientMods": len(validated["client"]["resolved"]), "resolvedServerMods": len(validated["server"]["resolved"]),
               "unusedSideRules": pack["unusedOverrides"],
               "coreUpdates": [n["id"] for n in pack["coreChanges"]],
               "fingerprint": pack["fingerprint"], "restartRequired": plan["restartRequired"], "changedFiles": len(plan["changes"])}
    if args.action in ("sync", "refresh-core"):
        summary.update(apply(pack, plan))
    if not args.quiet or summary.get("outcome") != "unchanged":
        print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    try:
        with pack_lock():
            main()
    except (PackError, OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(2)
