"""Exercise one-source add/update/remove through actual NeoForge A/B clients and dedicated server."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import time
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("modpack_runtime_test", ROOT / "tools/Modpack.py")
modpack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(modpack)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--powershell", required=True)
    parser.add_argument("--update-client-runtime", action="store_true",
                        help="Deploy the newly built native client during the first guarded restart")
    args = parser.parse_args()
    stamp = str(time.time_ns())
    output = ROOT / "analysis/goldcraft-tests" / f"modpack-runtime-{stamp}.json"
    build = modpack.safe(ROOT / "sandbox/modpack-probe-build" / stamp)
    build.mkdir(parents=True)
    report = {"source": __doc__, "checks": {}, "phases": {}, "commands": [], "fixtureRoot": str(build)}
    installed = {}
    rules_path = modpack.PACK / "rules.json"
    original_rules = rules_path.read_bytes()
    fixture_rules = original_rules

    def check(name, condition):
        report["checks"][name] = bool(condition)
        print(json.dumps({"check": name, "passed": bool(condition)}), flush=True)
        if not condition:
            raise AssertionError(name)

    def marker(kind, version):
        nonlocal fixture_rules
        mod_id = f"goldcraft_sync_probe_{kind}"
        cls = kind.capitalize() + "Probe"
        directory = build / f"{kind}-{version}"
        directory.mkdir()
        source = directory / f"{cls}.java"
        if modpack.IS_NEOFORGE:
            source.write_text(f'''package dev.goldcraft.packprobe;
import java.nio.file.Files;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLEnvironment;
import net.neoforged.fml.loading.FMLPaths;
import net.minecraft.resources.ResourceLocation;
@Mod("{mod_id}")
public final class {cls} {{
    public {cls}() {{
        String side = FMLEnvironment.dist.isClient() ? "client" : "server";
        String minecraftId = ResourceLocation.fromNamespaceAndPath("{mod_id}", side).toString();
        String data = "{{\\\"version\\\":\\\"{version}\\\",\\\"environment\\\":\\\"" + side
            + "\\\",\\\"pid\\\":" + ProcessHandle.current().pid()
            + ",\\\"minecraftId\\\":\\\"" + minecraftId + "\\\"}}";
        try {{ Files.writeString(FMLPaths.GAMEDIR.get().resolve("goldcraft-sync-probe-{kind}.json"), data); }}
        catch (Exception error) {{ throw new IllegalStateException(error); }}
    }}
}}''', encoding="utf-8")
            neo = modpack.PLATFORM["loader"]
            game = modpack.PLATFORM["minecraft"] + "-" + modpack.MINECRAFT_PIN["neoform"]
            libraries = ROOT / f".tools/neoforge-runtime/{neo}/libraries"
            loader = ";".join([str(libraries / f"net/neoforged/neoforge/{neo}/neoforge-{neo}-client.jar"),
                str(libraries / f"net/minecraft/client/{game}/client-{game}-srg.jar"),
                *modpack.read_json(ROOT / "build/neoforge-production/runClient.json")["classpath"]])
        else:
            source.write_text(f'''package dev.goldcraft.packprobe;
import java.nio.file.Files;
import net.fabricmc.api.ModInitializer;
import net.fabricmc.loader.api.FabricLoader;
public class {cls} implements ModInitializer {{
    public void onInitialize() {{
        var loader = FabricLoader.getInstance();
        String data = "{{\\\"version\\\":\\\"{version}\\\",\\\"environment\\\":\\\"" + loader.getEnvironmentType()
            + "\\\",\\\"pid\\\":" + ProcessHandle.current().pid() + "}}";
        try {{ Files.writeString(loader.getGameDir().resolve("goldcraft-sync-probe-{kind}.json"), data); }}
        catch (Exception error) {{ throw new IllegalStateException(error); }}
    }}
}}''', encoding="utf-8")
            loader = modpack.cached_jar("net.fabricmc", "fabric-loader", modpack.PLATFORM["loader"])
        subprocess.run([str(modpack.JAVAC), "-encoding", "UTF-8", "-cp", str(loader), "-d", str(directory), str(source)],
                       cwd=ROOT, check=True, capture_output=True)
        jar = directory / f"{mod_id}-{version}.jar"
        with ZipFile(jar, "w") as archive:
            if modpack.IS_NEOFORGE:
                archive.writestr("META-INF/neoforge.mods.toml", f'''modLoader="javafml"
loaderVersion="[4,)"
license="Test fixture"
[[mods]]
modId="{mod_id}"
version="{version}"
''')
            else:
                archive.writestr("fabric.mod.json", json.dumps({"schemaVersion": 1, "id": mod_id, "version": version,
                    "environment": "*" if kind == "shared" else kind,
                    "entrypoints": {"main": [f"dev.goldcraft.packprobe.{cls}"]},
                    "depends": {"minecraft": "1.21", "fabricloader": "0.16.14"}}))
            for file in directory.rglob("*.class"):
                archive.write(file, file.relative_to(directory).as_posix())
        destination = modpack.safe(modpack.SOURCE / "mods" / jar.name)
        if destination.exists():
            raise RuntimeError(f"Reserved fixture filename is already in use: {destination.name}")
        modpack.atomic_bytes(destination, jar.read_bytes())
        installed[destination] = modpack.sha(destination)
        if modpack.IS_NEOFORGE and kind != "shared":
            if rules_path.read_bytes() != fixture_rules:
                raise RuntimeError("Mod side rules changed externally during the runtime test")
            rules = json.loads(fixture_rules.decode("utf-8-sig"))
            rules.setdefault("sideOverrides", {})[mod_id] = kind
            modpack.write_json(rules_path, rules)
            fixture_rules = rules_path.read_bytes()
        return destination

    def remove(path):
        modpack.safe(path)
        if path.exists():
            if modpack.sha(path) != installed[path]:
                raise RuntimeError("The test mod changed externally; do not remove it automatically")
            path.unlink()

    def restart(phase):
        logfile = build / f"{phase}-restart.log"
        command = [args.powershell, "-NoLogo", "-NoProfile", "-File", str(ROOT / "tools/Sync-Modpack.ps1"), "-Restart", "-Start", "-Loader", modpack.LOADER]
        if args.update_client_runtime and phase == "added":
            command.append("-UpdateClientRuntime")
        report["commands"].append({"phase": phase, "command": command, "log": str(logfile)})
        print(json.dumps({"phase": phase, "status": "restarting isolated cluster"}), flush=True)
        with logfile.open("w", encoding="utf-8") as stream:
            result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, timeout=180)
        if result.returncode:
            raise RuntimeError(f"Cluster startup failed; see {logfile}")

    def evidence(phase, shared_version=None, removed=False):
        deadline = time.monotonic() + 150
        while True:
            try:
                result = {}
                for name, (side, directory) in modpack.TARGETS.items():
                    role = "MinecraftServer" if name == "cs-server" else "MinecraftClient"
                    process = modpack.read_json(ROOT / "sandbox" / name / f"process-{role}.json")
                    data = {"pid": process["pid"], "mods": {p.name: modpack.sha(p) for p in (directory / "mods").glob("*.jar")}}
                    if removed:
                        log = directory / "logs/latest.log"
                        text = log.read_text(encoding="utf-8", errors="replace")
                        loader_started = (f"NeoForge mod loading, version {modpack.PLATFORM['loader']}, for MC {modpack.PLATFORM['minecraft']}" in text if modpack.IS_NEOFORGE
                                          else "Loading Minecraft 1.21 with Fabric Loader 0.16.14" in text)
                        if log.stat().st_mtime < time.time() - 180 or not loader_started:
                            raise ValueError("New Loader startup not observed")
                        if "goldcraft_sync_probe_" in text or any("goldcraft_sync_probe_" in f for f in data["mods"]):
                            raise ValueError("Removed probe is still present")
                        data["loaderLog"] = str(log)
                    else:
                        data["markers"] = {}
                        for kind in ("shared", side):
                            marker_data = modpack.read_json(directory / f"goldcraft-sync-probe-{kind}.json")
                            expected = shared_version if kind == "shared" else "1.0.0"
                            if marker_data["pid"] != process["pid"] or marker_data["version"] != expected or marker_data["environment"].lower() != side:
                                raise ValueError("Probe belongs to an earlier process or version")
                            if modpack.IS_NEOFORGE and marker_data.get("minecraftId") != f"goldcraft_sync_probe_{kind}:{side}":
                                raise ValueError("The probe did not resolve ordinary Mojang-named Minecraft classes")
                            data["markers"][kind] = marker_data
                    result[name] = data
                report["phases"][phase] = result
                return result
            except (OSError, ValueError, KeyError):
                if time.monotonic() >= deadline:
                        raise TimeoutError(f"Actual {modpack.LOADER} {phase} markers did not become ready")
                time.sleep(.25)

    try:
        if list((modpack.SOURCE / "mods").glob("goldcraft_sync_probe_*.jar")):
            raise RuntimeError("A previous fixture is present; inspect it before rerunning")
        shared = marker("shared", "1.0.0")
        marker("client", "1.0.0")
        marker("server", "1.0.0")
        restart("added")
        data = evidence("added", "1.0.0")
        check("shared mod entrypoint runs in both clients and the actual dedicated server", len(data) == 3)
        check("client-only mod is absent from server deployment", not any("probe_client" in name for name in data["cs-server"]["mods"]))
        check("server-only mod is absent from both client deployments", all(not any("probe_server" in f for f in data[name]["mods"]) for name in ("cs-client-a", "cs-client-b")))
        check("A and B receive identical client jar hashes", data["cs-client-a"]["mods"] == data["cs-client-b"]["mods"])
        remove(shared)
        marker("shared", "2.0.0")
        restart("updated")
        updated = evidence("updated", "2.0.0")
        check("single-source update loads version 2 on all three Minecraft processes", len(updated) == 3)
        check("old version jar is removed from every runtime", all(not any("shared-1.0.0" in f for f in item["mods"]) for item in updated.values()))
        for path in installed:
            remove(path)
        restart("removed")
        cleaned = evidence("removed", removed=True)
        check("single-source removal clears actual Loader startup on all three runtimes", len(cleaned) == 3)
        report["outcome"] = "passed"
    except Exception as error:
        report["outcome"] = "incomplete"
        report["error"] = f"{type(error).__name__}: {error}"
    finally:
        for path in installed:
            try:
                remove(path)
            except Exception as error:
                report.setdefault("cleanupErrors", []).append(str(error))
        if rules_path.read_bytes() == fixture_rules:
            modpack.atomic_bytes(rules_path, original_rules)
        else:
            report.setdefault("cleanupErrors", []).append("Rules changed externally; fixture side rules were not automatically removed")
        output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        print(json.dumps({"report": str(output), "outcome": report["outcome"], "checks": len(report["checks"]), "error": report.get("error")}), flush=True)
    return 0 if report["outcome"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
