"""Capture matched Renderer shadow/emitter comparisons in the isolated cs_militia fixture."""
import argparse
import importlib.util
import json
import shutil
import socket
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIMENSION = "goldcraft:cs_militia_66f1b9b6"
SERVER_STATUS = ROOT / "sandbox/cs-server/logs/goldcraft-server-status.json"
spec = importlib.util.spec_from_file_location("minecraft_command", ROOT / "tools/Minecraft-Command.py")
rcon = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rcon)


def read_json(path):
    # Native diagnostics rewrite files; Java may replace them between two reads on Windows.
    deadline=time.monotonic()+2
    while True:
        try:return json.loads(path.read_text(encoding="utf-8-sig"))
        except (ValueError,OSError):
            if time.monotonic()>=deadline:raise
            time.sleep(.01)


class Sandbox:
    def __init__(self,instance="cs-client-a"):
        if instance not in ("cs-client-a","cs-client-b"):
            raise ValueError("Only the two isolated validation instances are supported")
        self.instance=instance
        self.player="GoldCraft_A" if instance.endswith("-a") else "GoldCraft_B"
        self.logs = ROOT / "sandbox" / instance / "logs"
        self.command_file = self.logs / "goldcraft-test-command.txt"
        self.sequence = max(int(self.command_file.read_text().split()[0]), time.time_ns() // 1_000_000)
        self.request = 1
        config = json.loads((ROOT / "sandbox/cluster.json").read_text(encoding="utf-8-sig"))
        self.connection = socket.create_connection(("127.0.0.1", config["minecraftRconPort"]), timeout=10)
        rcon.exchange(self.connection, 1, 3, config["rconToken"])
        self.commands = []

    def minecraft(self, command):
        self.request += 1
        response = rcon.exchange(self.connection, self.request, 2, command)
        self.commands.append({"command": command, "response": response})
        return response

    def status(self):
        return read_json(self.logs / "goldcraft-client-status.json")

    def host(self, command):
        # Another sequential test may have used this file since construction.
        # Never acknowledge a newer command as if this command had executed.
        self.sequence = max(self.sequence, self.status()["testCommand"],
                            int(self.command_file.read_text().split()[0]),
                            time.time_ns() // 1_000_000) + 1
        temporary = self.command_file.with_suffix(".tmp")
        temporary.write_text(f"{self.sequence} {command}\n", encoding="ascii")
        temporary.replace(self.command_file)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            observed = self.status()["testCommand"]
            if observed == self.sequence:
                return
            if observed > self.sequence:
                raise RuntimeError("A concurrent sandbox command replaced this test command")
            time.sleep(0.02)
        raise TimeoutError(f"Native command did not acknowledge {command}")

    def capture(self, output):
        source = self.logs / "goldcraft-frame.bmp"
        previous = source.stat().st_mtime_ns if source.exists() else 0
        before = self.status()
        self.host("capture")
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if source.exists() and source.stat().st_mtime_ns > previous:
                # capture() closes the BMP before publishing its next status.
                time.sleep(0.1)
                shutil.copy2(source, output)
                after = self.status()
                return {"image": str(output.relative_to(ROOT)), "before": before, "after": after,
                        "stable": same_view(before, after)}
            time.sleep(0.02)
        raise TimeoutError("No new native framebuffer capture")


def same_view(a, b):
    return (a["world"] == b["world"] and
            all(abs(x - y) < 0.003 for x, y in zip(a["minecraftFeet"], b["minecraftFeet"])) and
            all(abs(x - y) < 0.05 for x, y in zip(a["viewAngles"], b["viewAngles"])) and
            a["inputButtons"] == b["inputButtons"] == 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--comparison", choices=("shadows", "emitter"), default="shadows")
    parser.add_argument("--position", nargs=3, type=float, default=(1.25, 60.001, 72.0))
    parser.add_argument("--view", nargs=2, type=float, default=(-25, 8), metavar=("YAW", "PITCH"))
    args = parser.parse_args()
    sandbox = Sandbox()
    prefix = ROOT / "analysis/goldcraft-tests" / f"renderer-{args.comparison}-{int(time.time())}"
    report = {"comparison": args.comparison, "source": "Actual MetaHook Renderer final draw framebuffer",
              "commands": sandbox.commands, "captures": []}
    try:
        initial = sandbox.status()
        if not (initial["minecraftControl"] and initial["sceneApi"] and initial["vertices"]):
            raise RuntimeError("Nonempty controlled Renderer scene is not ready")
        sandbox.minecraft(f"execute in {DIMENSION} run tp @e[type=minecraft:pig,tag=goldcraft_shadow_test] 6.5 60 76.5")
        sandbox.minecraft(f"execute as GoldCraft_A in {DIMENSION} run tp @s {' '.join(map(str, args.position))}")
        sandbox.host(f"view {args.view[0]} {args.view[1]}")
        time.sleep(0.8)
        if args.comparison == "shadows":
            for enabled in (0, 1):
                sandbox.host(f"shadows {enabled}")
                time.sleep(0.7)
                capture = sandbox.capture(prefix.with_name(prefix.name + f"-{enabled}.bmp"))
                capture["enabled"] = bool(enabled)
                report["captures"].append(capture)
        else:
            sandbox.host("shadows 1")
            for enabled in (1, 0):
                if enabled:
                    sandbox.minecraft(f"execute in {DIMENSION} run setblock 3 63 77 minecraft:glowstone keep")
                else:
                    sandbox.minecraft(f"execute in {DIMENSION} if block 3 63 77 minecraft:glowstone run setblock 3 63 77 minecraft:air")
                time.sleep(1.5)
                capture = sandbox.capture(prefix.with_name(prefix.name + f"-{enabled}.bmp"))
                capture["enabled"] = bool(enabled)
                report["captures"].append(capture)
        first, second = report["captures"]
        report["matchedView"] = first["stable"] and second["stable"] and same_view(first["after"], second["before"])
        report["glErrors"] = [c["after"]["glError"] for c in report["captures"]]
        if args.comparison == "emitter":
            report["lightCounts"] = [c["after"]["lights"] for c in report["captures"]]
        report["outcome"] = "matched images ready for visual inspection" if report["matchedView"] else "inconclusive: camera or input changed"
    finally:
        sandbox.host("shadows 1")
        if args.comparison == "emitter":
            sandbox.minecraft(f"execute in {DIMENSION} run setblock 3 63 77 minecraft:glowstone keep")
        sandbox.connection.close()
        prefix.with_suffix(".json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"report": str(prefix.with_suffix('.json')), **{k: report[k] for k in ("outcome", "matchedView", "glErrors")}}, indent=2))


if __name__ == "__main__":
    main()
