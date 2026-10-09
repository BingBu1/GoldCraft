"""Real production NeoForge -> ReHLDS host-map mining, in an independent fixture.

Uses a NeoForge FakePlayer registered in the isolated player list and a real
native CBasePlayer fake client. Held intent is simulated; production HostMining,
raycasts, pairing, sockets, native damage and item durability execute unchanged.
This does not prove client input, network login, movement physics or excavation.
Never launches or stops the primary server. Run in a PTY, as required by ReHLDS.
"""
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parent.parent


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


mining = load("mining_native", "Exercise-MapMining.py")
runtime = load("mining_runtime", "Exercise-NeoForgeCompatibility.py")
WORK = mining.base.TEST / "mining-jvm"
BUILD = ROOT / "build/map-mining-probe"


class Run(mining.Run):
    def __init__(self):
        super().__init__()
        self.jvm = None
        self.jvm_log = None
        self.command_id = 0
        self.report["scope"] = __doc__

    def compile_probe(self):
        BUILD.mkdir(parents=True, exist_ok=True)
        classes = BUILD / "classes"
        classes.mkdir(exist_ok=True)
        manifest = json.loads((ROOT / "build/neoforge-production/runServer.json").read_text(encoding="utf-8"))
        libraries = ROOT / f".tools/neoforge-runtime/{runtime.NEOFORGE}/libraries"
        jars = [libraries / f"net/neoforged/neoforge/{runtime.NEOFORGE}/neoforge-{runtime.NEOFORGE}-server.jar",
                libraries / f"net/minecraft/server/{runtime.MAPPED_GAME}/server-{runtime.MAPPED_GAME}-srg.jar",
                libraries / f"net/neoforged/neoforge/{runtime.NEOFORGE}/neoforge-{runtime.NEOFORGE}-universal.jar",
                ROOT / "neoforge/build/libs/goldcraft-neoforge-0.1.0-dev.jar"]
        legacy = next(v.split("=", 1)[1] for v in manifest["jvm"] if v.startswith("-DlegacyClassPath="))
        arguments = BUILD / "javac.args"
        runtime.argfile(arguments, ["--release", "21", "-proc:none", "-encoding", "UTF-8", "-cp",
            ";".join([*(str(p) for p in jars), legacy]), "-d", classes, ROOT / "tools/java/MapMiningProbe.java"])
        built = subprocess.run([str(runtime.JAVA.with_name("javac.exe")), "@" + str(arguments)], cwd=BUILD, capture_output=True)
        (BUILD / "javac.log").write_bytes(built.stdout + built.stderr)
        if built.returncode:
            raise RuntimeError("Probe compile failed: build/map-mining-probe/javac.log")
        probe = BUILD / "goldcraft_mining_probe.jar"
        with ZipFile(probe, "w") as out:
            out.writestr("META-INF/neoforge.mods.toml", 'modLoader="javafml"\nloaderVersion="[4,)"\nlicense="Test fixture"\n'
                '[[mods]]\nmodId="goldcraft_mining_probe"\nversion="1.0.0"\n')
            # Exactly this fixture, never stale classes from other probe builds.
            entry = "dev/goldcraft/test/mining/MapMiningProbe.class"
            out.write(classes / entry, entry)
        return manifest, jars[-1], probe

    def launch_jvm(self):
        manifest, core, probe = self.compile_probe()
        for directory in (WORK, WORK / "mods", WORK / "tmp"):
            mining.base.safe(directory)
            directory.mkdir(parents=True, exist_ok=True)
        self.copy(core, WORK / "mods/goldcraft.jar")
        self.copy(probe, WORK / "mods/goldcraft_mining_probe.jar")
        self.put(WORK / "eula.txt", b"eula=true\n")
        self.put(WORK / "server.properties", (
            'server-ip=127.0.0.1\nserver-port=0\nonline-mode=true\nenable-rcon=false\n'
            'level-name=world\nlevel-type=minecraft\\:flat\ngenerate-structures=false\n'
            'view-distance=2\nsimulation-distance=2\nspawn-protection=0\nmax-players=4\n'
            'generator-settings={"biome":"minecraft:plains","features":false,"lakes":false,"layers":[{"height":1,"block":"minecraft:air"}]}\n'
        ).encode())
        datapack = mining.base.TEST / "minecraft/world/datapacks/goldcraft_headless"
        for source in datapack.rglob("*"):
            if source.is_file():
                self.copy(source, WORK / "world/datapacks/goldcraft_headless" / source.relative_to(datapack))
        self.put(WORK / "command.json", b'{"id":0,"op":"held","value":false}')
        self.put(WORK / "status.json", b"{}")
        args = ["-Xms256m", "-Xmx1024m", *[v for v in manifest["jvm"] if not v.startswith(("-Xms", "-Xmx"))],
                "-Djava.io.tmpdir=" + str(WORK / "tmp")]
        if manifest["classpath"]:
            args += ["-cp", ";".join(manifest["classpath"])]
        args += [manifest["main"], *manifest["args"]]
        argument_file = WORK / "server.args"
        self.before[argument_file] = argument_file.read_bytes() if argument_file.exists() else None
        runtime.argfile(argument_file, args)
        environment = {k: v for k, v in os.environ.items() if not k.startswith("GOLDCRAFT_")}
        for key in manifest.get("clearEnvironment", []):
            environment.pop(key, None)
        environment.update(manifest.get("environment", {}))
        for field in ("PORT", "SESSION", "TOKEN"):
            environment["GOLDCRAFT_SERVER_" + field] = str(self.config["server" + field.title()])
        environment.update(GOLDCRAFT_HEADLESS_BINDINGS=str(self.bindings),
            GOLDCRAFT_MINING_COMMAND=str(WORK / "command.json"), GOLDCRAFT_MINING_PROBE=str(WORK / "status.json"))
        self.jvm_log = (WORK / "runtime.log").open("wb")
        self.jvm = subprocess.Popen([str(runtime.JAVA), "@" + str(argument_file)], cwd=WORK, env=environment,
            stdout=self.jvm_log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
        self.report["minecraftPid"] = self.jvm.pid
        print(json.dumps({"minecraftPid": self.jvm.pid, "loader": runtime.NEOFORGE, "minecraft": runtime.MINECRAFT}), flush=True)

    def retry_io(self, operation, label):
        deadline = time.monotonic() + 2
        while True:
            if self.jvm and self.jvm.poll() is not None:
                raise RuntimeError(f"Mining JVM exited {self.jvm.returncode}; inspect isolated runtime.log")
            try:
                return operation()
            except (PermissionError, FileNotFoundError, json.JSONDecodeError) as error:
                if time.monotonic() >= deadline:
                    raise TimeoutError(label) from error
                time.sleep(.02)

    def probe(self):
        def read():
            with (WORK / "status.json").open(encoding="utf-8") as stream:
                value = json.load(stream)
                # Read the timestamp from the same open sample, not a replacement
                # written after it. A stalled JVM must not pass from old state.
                if value.get("tick") and time.time_ns() - os.fstat(stream.fileno()).st_mtime_ns > 5_000_000_000:
                    raise RuntimeError("Mining JVM status is stale")
                return value
        value = self.retry_io(read, "Mining JVM status remains unavailable")
        if value.get("error"):
            raise RuntimeError("Minecraft fixture: " + value["error"])
        return value

    def publish(self, op, **values):
        self.command_id += 1
        data = dict(id=self.command_id, op=op, **values)
        temporary = WORK / "command.json.tmp"
        temporary.write_text(json.dumps(data), encoding="utf-8")
        self.retry_io(lambda: temporary.replace(WORK / "command.json"), "Mining command remains unavailable")
        return data

    def action(self, op, **values):
        data = self.publish(op, **values)
        result = self.until(lambda: (p if (p := self.probe()).get("command") == self.command_id else None), "Fixture command " + op)
        self.report.setdefault("minecraftCommands", []).append({"command": data, "result": result})
        return result

    def ticks(self, count):
        tick = self.probe()["tick"]
        return self.until(lambda: (p if (p := self.probe()).get("tick", 0) >= tick + count else None), "Minecraft ticks")

    def target(self, model):
        answer = self.command("gc_mining_probe " + str(model))
        match = re.search(r"target=(\d+) eye=([^ ]+) point=([^ ]+)", answer)
        if not match:
            raise RuntimeError("No native target approach")
        self.target_slot = int(match[1])
        eye, point = [[float(v) for v in match[n].split(",")] for n in (2, 3)]
        self.action("aim", eye=eye, point=point)
        self.until(lambda: self.probe().get("target") == self.target_slot, "Vanilla world raycast reaches host brush")
        self.ticks(4)

    def native_state(self):
        return {k: float(v) for k, v in re.findall(r"(\w+)=(-?[\d.]+)", self.command("gc_mining_state"))}

    def unchanged(self, label, ticks=12):
        before = self.native_state()
        self.action("held", value=True)
        self.ticks(ticks)
        self.action("held", value=False)
        self.ticks(3)
        after = self.native_state()
        self.check(label, before["health"] == after["health"] and before["calls"] == after["calls"])

    def run(self):
        inventory = subprocess.check_output(["powershell.exe", "-NoProfile", "-Command",
            "Get-CimInstance Win32_Process | Where-Object { $_.Name -in @('java.exe','javaw.exe') } | Select-Object ProcessId,CommandLine | ConvertTo-Json -Compress"],
            text=True, creationflags=subprocess.CREATE_NO_WINDOW)
        rows = json.loads(inventory) if inventory.strip() else []
        for entry in rows if isinstance(rows, list) else [rows]:
            if str(WORK).lower() in str(entry.get("CommandLine", "")).replace("/", "\\").lower():
                raise RuntimeError("Mining JVM fixture already running; no files changed")
        self.start_native()
        def native_ready():
            try:
                return "ReHLDS" in self.command("version")
            except OSError:
                return False
        self.until(native_ready, "ReHLDS startup", 45)
        self.check("independent native server uses sv_lan 0", '"sv_lan" is "0"' in self.command("sv_lan"))
        self.command("mc_default_form 1"); self.command("mc_allow_switch 1")
        self.slot = int(re.search(r"slot=(\d+)", self.command("gc_headless_create"))[1])
        self.launch_jvm()
        self.until(lambda: self.probe().get("actorFlags", 0) & 32, "Real JVM paired and owns native control", 100)
        self.until(lambda: not self.probe()["freeze"], "Native freeze period ends")
        self.check("production Minecraft receives real authority and policy", self.probe()["connected"] and self.probe()["map"] == "cs_assault" and self.probe()["mode"] == 0)
        self.target(56)
        self.command("gc_mining_edit 100 1")
        self.unchanged("mode 0 blocks real Minecraft mining before native damage")
        self.command("mc_map_mining 1")
        self.until(lambda: self.probe()["mode"] == 1, "Policy update in real JVM")
        self.action("held", value=True)
        self.until(lambda: self.native_state().get("health", 100) < 100, "Actual Minecraft tool damages native glass")
        self.action("held", value=False)
        self.ticks(4)
        damage = self.native_state()
        self.check("real JVM raycast/tool request reaches native TakeDamage with paired attacker", 0 < damage["health"] < 100 and damage["attacker"] == self.slot)
        self.check("partial native damage does not consume breaking durability", self.probe()["toolDamage"] == 0)
        self.ticks(20)
        self.check("released intent stops repeated damage", self.native_state()["health"] == damage["health"])
        self.command("gc_mining_edit 1000 1")
        calls = self.native_state()["calls"]
        self.action("held", value=True)
        self.until(lambda: self.native_state()["calls"] > calls, "Renewed intent damages glass")
        self.action("renew", value=False)
        self.ticks(12)  # Beyond the production 350 ms held-input lease.
        expired = self.native_state()
        self.ticks(22)
        after = self.native_state()
        self.check("lost input heartbeat stops held mining", self.probe()["held"] and not self.probe()["renew"]
                   and after["calls"] == expired["calls"] and after["health"] == expired["health"])
        self.action("held", value=False)
        self.action("renew", value=True)
        calls = self.native_state()["calls"]
        self.action("held", value=True)
        self.until(lambda: self.native_state()["calls"] > calls, "Fresh intent resumes mining")
        self.command("mc_map_mining 0")
        self.until(lambda: self.probe()["mode"] == 0, "Disabled policy reaches real JVM")
        disabled = self.native_state()
        self.ticks(22)
        after = self.native_state()
        self.check("policy hot disable stops ongoing held mining", after["calls"] == disabled["calls"]
                   and after["health"] == disabled["health"] and self.probe()["toolDamage"] == 0)
        self.action("held", value=False)
        self.command("mc_map_mining 1")
        self.until(lambda: self.probe()["mode"] == 1, "Enabled policy reaches real JVM")
        damage = self.native_state()
        self.action("mode", value="adventure")
        self.unchanged("adventure mode cannot mine host entities")
        self.action("mode", value="survival")
        self.action("use", value=True)
        self.unchanged("using an item suppresses mining")
        self.action("use", value=False)
        self.action("block", value=True)
        self.check("Minecraft block wins over the farther host raycast", self.probe()["target"] == -1)
        self.unchanged("Minecraft block occlusion prevents host damage")
        self.action("block", value=False)
        self.command("gc_mining_filter 1")
        self.action("held", value=True)
        calls = self.native_state()["calls"]
        self.until(lambda: self.native_state()["calls"] > calls, "Real native Ham cancellation")
        self.action("held", value=False)
        self.check("native cancellation leaves glass health and tool durability unchanged", self.native_state()["health"] == damage["health"] and self.probe()["toolDamage"] == 0)
        self.command("gc_mining_filter 0")
        self.command("gc_mining_edit 3 1")
        self.action("held", value=True)
        self.until(lambda: self.probe()["toolDamage"] == 1, "Native break result reaches real tool postMine")
        self.action("held", value=False)
        self.check("confirmed native break consumes survival tool durability once", self.probe()["toolDamage"] == 1)
        self.ticks(20)
        self.check("removed native brush disappears from Minecraft raycast", self.probe()["target"] != self.target_slot and self.probe()["toolDamage"] == 1)
        self.target(67)
        self.action("held", value=True)
        self.ticks(20)
        self.action("held", value=False)
        self.check("unbreakable glass never consumes tool durability", self.probe()["toolDamage"] == 1)
        self.target(77)
        self.command("gc_mining_edit 100 1")
        self.action("mode", value="creative")
        self.action("held", value=True)
        destroyed = self.until(lambda: (s if (s := self.native_state()).get("removed") == 1
            or s.get("solid") == 0 or s.get("health", 100) <= 0 else None), "Creative native break")
        self.action("held", value=False)
        self.ticks(5)
        self.check("creative mode breaks native glass with paired attacker", destroyed["calls"] > 0 and destroyed["attacker"] == self.slot)
        self.check("creative native break consumes no durability", self.probe()["toolDamage"] == 1)
        self.report["finalMinecraft"] = self.probe()
        self.report["runtimeErrors"] = {p.name: p.read_bytes()[self.error_offsets.get(p, 0):].decode("utf-8", errors="replace")
            for p in (mining.base.AMXX / "logs").glob("error_*.log") if p.stat().st_size > self.error_offsets.get(p, 0)}
        self.check("AMXX/ReAPI fixture has no new runtime errors", not self.report["runtimeErrors"])
        self.report["passed"] = True

    def close(self):
        try:
            if not self.report.get("passed"):
                for name, observe in (("failureMinecraft", self.probe), ("failureNative", self.state)):
                    try:
                        self.report[name] = observe()
                    except (OSError, ValueError, RuntimeError):
                        pass
            if self.jvm and self.jvm.poll() is None:
                try:
                    self.publish("stop")
                    self.jvm.wait(timeout=30)
                except (OSError, RuntimeError, subprocess.TimeoutExpired) as failure:
                    self.report["minecraftShutdownError"] = str(failure)
                finally:
                    if self.jvm.poll() is None:
                        self.jvm.terminate(); self.jvm.wait(timeout=10)
            if self.jvm:
                self.report["minecraftExitCode"] = self.jvm.returncode
                self.report["minecraftStopped"] = self.jvm.poll() is not None
                if self.jvm.returncode != 0 or "minecraftShutdownError" in self.report:
                    self.report["passed"] = False
            if self.jvm_log:
                self.jvm_log.close()
        finally:
            super().close(output=ROOT / f"analysis/world-carving/mining-jvm-{mining.STAMP}.json")


if __name__ == "__main__":
    run = Run()
    try:
        run.run()
    except Exception as error:
        run.report["error"] = str(error)
        run.report["passed"] = False
        raise
    finally:
        run.close()
    sys.exit(0 if run.report["passed"] else 1)
