"""Test a normal Mojang-named NeoForge Mod and the remapped core in an isolated JVM.

The probe references a real Minecraft class. A metadata-only or FML-only marker
cannot detect a mismatch between the game's and third-party Mods' mappings.
No running CS/MC pair is addressed, stopped or restarted.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import time
from zipfile import ZipFile
import Modpack
from Modpack import MINECRAFT_PIN

ROOT = Path(__file__).resolve().parent.parent
JAVA = ROOT / ".tools/java/jdk-21.0.12.1+1/bin/java.exe"
NEOFORGE = MINECRAFT_PIN["neoforge"]
MINECRAFT = MINECRAFT_PIN["version"]
MAPPED_GAME = MINECRAFT + "-" + MINECRAFT_PIN["neoform"]


def argfile(path, arguments):
    path.write_text("\n".join('"' + str(v).replace("\\", "\\\\").replace('"', '\\"') + '"' for v in arguments), encoding="utf-8")


def select_fixture_pack(pack, excluded, extra_mods, replacements=()):
    """Select test inputs in memory; never edit the XMCL master or deployment state."""
    excluded = set(excluded)
    available = {mod_id for node in pack["nodes"] for mod_id in node["metadata"].get("ids", [node["id"]])}
    if excluded - available:
        raise ValueError(f"Not a top-level managed Mod: {sorted(excluded - available)}")
    if "goldcraft" in excluded:
        raise ValueError("The compatibility probe requires the managed GoldCraft core")
    nodes = [node for node in pack["nodes"] if excluded.isdisjoint(node["metadata"].get("ids", [node["id"]]))]
    for extra, replace in [*((path, True) for path in replacements), *((path, False) for path in extra_mods)]:
        path = Modpack.safe(extra.resolve(), ROOT)
        if not path.is_file() or path.suffix != ".jar":
            raise ValueError("Extra Mod must be a distinct workspace JAR")
        data = path.read_bytes()
        node = Modpack.scan_jar(data, str(path))
        old = next((value for value in nodes if value["id"] == node["metadata"]["id"]), None)
        if replace:
            if old is None:
                raise ValueError(f"Replacement Mod is not in the selected pack: {node['metadata']['id']}")
            nodes = [value for value in nodes if value is not old]
        ids = {mod_id for value in nodes for mod_id in value["metadata"].get("ids", [value["id"]])}
        names = {value["name"].lower() for value in nodes}
        provided = set(node["metadata"].get("ids", [node["metadata"]["id"]]))
        if ids.intersection(provided) or excluded.intersection(provided) or path.name.lower() in names:
            raise ValueError(f"Extra Mod duplicates an included or excluded ID: {sorted(provided)}")
        node.update(name=path.name, id=node["metadata"]["id"], version=node["metadata"]["version"],
                    sha256=hashlib.sha256(data).hexdigest(), sides=old["sides"] if replace else Modpack.side_assignment(node, {}))
        nodes.append(node)
    nodes.sort(key=lambda node: node["name"].lower())
    contract = {key: pack[key] for key in ("platform", "configs", "coreFromDevelopmentClasspath")}
    contract["jars"] = [{key: node[key] for key in ("name", "id", "version", "sha256", "sides")} for node in nodes]
    fingerprint = hashlib.sha256(json.dumps(contract, sort_keys=True).encode()).hexdigest()
    return {**pack, **contract, "nodes": nodes, "fingerprint": fingerprint}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", choices=("production", "development"), default="production")
    parser.add_argument("--modpack", action="store_true", help="Load the actual managed server Mod set, including JarJar dependencies")
    parser.add_argument("--refresh-core", action="store_true", help="Test the current core build in memory without updating the managed Mod source")
    parser.add_argument("--extra-mod", action="append", type=Path, default=[], help="Additional workspace Mod to test without changing the master")
    parser.add_argument("--replace-mod", action="append", type=Path, default=[], help="Test a replacement JAR with the same Mod ID only in this fixture")
    parser.add_argument("--exclude-mod", action="append", default=[], help="Omit one top-level Mod ID only from this isolated fixture; repeat for multiple IDs")
    parser.add_argument("--maid-checks", action="store_true", help="Exercise real maid creation, skill persistence, recipes, packets and combat ticks")
    options = parser.parse_args()
    runtime = options.runtime
    if options.modpack and runtime != "production":
        parser.error("Managed third-party Mods require the production runtime")
    if (options.exclude_mod or options.replace_mod or options.refresh_core) and not options.modpack:
        parser.error("--exclude-mod, --replace-mod and --refresh-core require --modpack")
    pack = Modpack.snapshot(refresh_core=options.refresh_core) if options.modpack else None
    source_fingerprint = pack["fingerprint"] if pack else None
    if pack and (options.exclude_mod or options.extra_mod or options.replace_mod):
        pack = select_fixture_pack(pack, options.exclude_mod, options.extra_mod, options.replace_mod)
    validated = Modpack.validate(pack) if pack else None
    stamp = str(time.time_ns())
    work = ROOT / "sandbox/neoforge-compatibility" / stamp
    work.mkdir(parents=True)
    manifest_file = ROOT / ("build/neoforge-production/runServer.json" if runtime == "production" else
                            "build/neoforge-runtime/runOfflineTestServer.json")
    manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    mojang = ROOT / f".tools/gradle-cache/caches/fabric-loom/minecraftMaven/net/minecraft/neoforge-{NEOFORGE}-minecraft-merged-mojang"
    jars = [p for p in mojang.rglob("*.jar") if not p.name.endswith("-sources.jar")]
    if runtime == "development" and len(jars) != 1:
        raise RuntimeError("Build the pinned NeoForge mapping artifacts first")
    source = work / "RuntimeProbe.java"
    source.write_text('''package dev.goldcraft.compatibility;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.minecraft.resources.ResourceLocation;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.nio.file.Files;
@Mod("goldcraft_runtime_probe")
public final class RuntimeProbe {
    public RuntimeProbe() throws Exception {
        var id = ResourceLocation.fromNamespaceAndPath("goldcraft_runtime_probe", "loaded");
        Files.writeString(FMLPaths.GAMEDIR.get().resolve("probe-loaded.txt"), id.toString());
        NeoForge.EVENT_BUS.addListener(RuntimeProbe::started);
    }
    private static void started(ServerStartedEvent event) {
        var level = event.getServer().getLevel(net.minecraft.world.level.Level.OVERWORLD);
        var block = level.getBlockState(net.minecraft.core.BlockPos.ZERO).getBlock();
        var id = net.minecraft.core.registries.BuiltInRegistries.BLOCK.getKey(block);
        try {
            var mods = new com.google.gson.JsonArray();
            for (var mod : net.neoforged.fml.ModList.get().getMods()) {
                var value = new com.google.gson.JsonObject();
                value.addProperty("id", mod.getModId());
                value.addProperty("version", mod.getVersion().toString());
                mods.add(value);
            }
            Files.writeString(FMLPaths.GAMEDIR.get().resolve("probe-loaded-mods.json"), mods.toString());
            Files.writeString(FMLPaths.GAMEDIR.get().resolve("probe-server-ready.txt"), id.toString());
        }
        catch (Exception error) { throw new IllegalStateException(error); }
    }
}
''', encoding="utf-8")
    classes = work / "classes"
    classes.mkdir()
    if runtime == "production":
        libraries = ROOT / f".tools/neoforge-runtime/{NEOFORGE}/libraries"
        game = libraries / f"net/neoforged/neoforge/{NEOFORGE}/neoforge-{NEOFORGE}-server.jar"
        vanilla = libraries / f"net/minecraft/server/{MAPPED_GAME}/server-{MAPPED_GAME}-srg.jar"
        universal = libraries / f"net/neoforged/neoforge/{NEOFORGE}/neoforge-{NEOFORGE}-universal.jar"
        legacy = next(v.split("=", 1)[1] for v in manifest["jvm"] if v.startswith("-DlegacyClassPath="))
        cp = ";".join([str(game), str(vanilla), str(universal), legacy])
    else:
        cp = ";".join([str(jars[0]), *manifest["classpath"]])
    args = work / "javac.args"
    extra_sources = []
    if options.maid_checks:
        addon_build = json.loads((ROOT / "build/epicfight-maid/build.json").read_text(encoding="utf-8"))
        cp += ";" + ";".join([addon_build["output"], *addon_build["compileClasspath"]])
        extra_sources.append(ROOT / "tools/java/EpicFightMaidProbe.java")
    argfile(args, ["--release", "21", "-proc:none", "-encoding", "UTF-8", "-cp", cp, "-d", classes, source, *extra_sources])
    compiled = subprocess.run([str(JAVA.with_name("javac.exe")), "@" + str(args)], cwd=work, capture_output=True)
    (work / "javac.log").write_bytes(compiled.stdout + compiled.stderr)
    if compiled.returncode:
        raise RuntimeError(f"Probe compilation failed: {work / 'javac.log'}")
    mods = work / "mods"
    mods.mkdir()
    for extra in options.extra_mod if pack is None else []:
        extra = extra.resolve()
        if not extra.is_relative_to(ROOT.resolve()) or not extra.is_file() or extra.suffix != ".jar" or (mods / extra.name).exists():
            raise RuntimeError("Extra Mod must be a distinct workspace JAR")
        shutil.copyfile(extra, mods / extra.name)
    with ZipFile(mods / "goldcraft_runtime_probe.jar", "w") as archive:
        archive.writestr("META-INF/neoforge.mods.toml", '''modLoader="javafml"
loaderVersion="[4,)"
license="Test fixture"
[[mods]]
modId="goldcraft_runtime_probe"
version="1.0.0"
''' + ('\n[[mods]]\nmodId="goldcraft_maid_probe"\nversion="1.0.0"\n' if options.maid_checks else ''))
        for file in classes.rglob("*.class"):
            archive.write(file, file.relative_to(classes).as_posix())
    (work / "eula.txt").write_text("eula=true\n", encoding="ascii")
    (work / "server.properties").write_text(
        "server-ip=127.0.0.1\nserver-port=0\nenable-rcon=false\nview-distance=2\nsimulation-distance=2\n"
        "level-type=minecraft:flat\ngenerate-structures=false\n"
        'generator-settings={"layers":[{"block":"minecraft:bedrock","height":1}],"biome":"minecraft:plains"}\n', encoding="ascii")
    if pack:
        for node in pack["nodes"]:
            if "server" in node["sides"]:
                source = Modpack.safe(Path(node["path"]), ROOT)
                destination = Modpack.safe(mods / node["name"])
                if destination.exists():
                    raise RuntimeError("Managed Mod conflicts with the reserved runtime probe")
                if Modpack.sha(source) != node["sha256"]:
                    raise RuntimeError("Managed Mod changed after validation")
                shutil.copyfile(source, destination)
        for node in pack["configNodes"]:
            source = Modpack.safe(Path(node["path"]))
            if Modpack.sha(source) != node["sha256"]:
                raise RuntimeError("Managed configuration changed after validation")
            Modpack.atomic_bytes(work / node["name"], source.read_bytes())
    elif runtime == "production":
        shutil.copyfile(ROOT / "neoforge/build/libs/goldcraft-neoforge-0.1.0-dev.jar", mods / "goldcraft.jar")
    (work / "tmp").mkdir()
    args = work / "server.args"
    classpath_args = ["-cp", ";".join(manifest["classpath"])] if manifest["classpath"] else []
    argfile(args, [*manifest["jvm"], f"-Djava.io.tmpdir={work / 'tmp'}", *classpath_args, manifest["main"], *manifest["args"]])
    env = {k: v for k, v in os.environ.items() if not k.startswith("GOLDCRAFT_")}
    for key in manifest.get("clearEnvironment", []):
        env.pop(key, None)
    env.update(manifest.get("environment", {}))
    print(json.dumps({"phase": f"isolated {runtime} JVM", "fixture": str(work)}), flush=True)
    with (work / "runtime.log").open("wb") as output:
        process = subprocess.Popen([str(JAVA), "@" + str(args)], cwd=work, env=env,
                                   stdin=subprocess.PIPE, stdout=output, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            deadline = time.monotonic() + 150
            while process.poll() is None and not ((work / "probe-server-ready.txt").exists()
                    and (not options.maid_checks or (work / "maid-checks.json").exists())):
                if time.monotonic() >= deadline:
                    raise subprocess.TimeoutExpired(process.args, 150)
                time.sleep(.25)
            if runtime == "production" and process.poll() is None:
                process.stdin.write(b"stop\n")
                process.stdin.flush()
            code = process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=15)
            raise RuntimeError("Isolated compatibility JVM timed out; only its owned process was stopped")
    text = (work / "runtime.log").read_text(encoding="utf-8", errors="replace")
    marker = work / "probe-loaded.txt"
    loaded_path = work / "probe-loaded-mods.json"
    loaded = json.loads(loaded_path.read_text(encoding="utf-8")) if loaded_path.exists() else []
    actual = {item["id"]: item["version"] for item in loaded}
    expected = {item["id"]: item["version"] for item in validated["server"]["resolved"]} if validated else {}
    maid_report = work / "maid-checks.json"
    maid = json.loads(maid_report.read_text(encoding="utf-8")) if maid_report.exists() else None
    result = {"loader": f"NeoForge {NEOFORGE}", "minecraft": MINECRAFT, "runtime": runtime, "exitCode": code,
              "ordinaryMojangNamedModLoaded": marker.exists(),
              "serverStartedWithMinecraftQueries": (work / "probe-server-ready.txt").exists(),
              "goldcraftLoaded": f"GoldCraft initialized: Minecraft {MINECRAFT} / NeoForge {NEOFORGE}" in text,
              "missingMojangClass": "NoClassDefFoundError: net/minecraft/resources/ResourceLocation" in text,
              "managedPackFingerprint": source_fingerprint,
              "candidateCore": options.refresh_core,
              "testedPackFingerprint": pack["fingerprint"] if pack else None,
              "excludedManagedMods": sorted(set(options.exclude_mod)),
              "replacementJars": [path.name for path in options.replace_mod],
              "excludedModsAbsent": all(mod_id not in actual for mod_id in options.exclude_mod),
              "loadedMods": loaded, "expectedManagedMods": expected,
              "managedModsMatch": all(actual.get(key) == value for key, value in expected.items()),
              "maidChecks": maid,
              "fixture": str(work), "log": str(work / "runtime.log")}
    report = ROOT / "analysis/goldcraft-tests" / f"neoforge-compatibility-{stamp}.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({**result, "report": str(report)}, indent=2))
    return 0 if code == 0 and (not options.maid_checks or (maid and maid.get("passed"))) and all(result[key] for key in (
        "ordinaryMojangNamedModLoaded", "serverStartedWithMinecraftQueries", "goldcraftLoaded", "managedModsMatch", "excludedModsAbsent")) else 1


if __name__ == "__main__":
    raise SystemExit(main())
