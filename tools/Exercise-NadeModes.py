"""Exercise the real Nade Modes/ReAPI/ZP stack in an isolated native fixture.

Controlled fake clients/factory calls/Touch/velocity/button inputs are explicit.
This is not graphical-client or autonomous-Bot acceptance. No primary-server
files, client input or C++ binaries are changed.
"""
from pathlib import Path
import argparse
import importlib.util
import json
import os
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("zp_fixture", ROOT / "tools/Exercise-ZombieReAPI.py")
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
STAMP = str(time.time_ns())
LOG = ROOT / "build/logs/nademodes-fixture.stdout.log"


class Run(base.Run):
    def __init__(self, quick=False, c4_only=False, media_high=False):
        super().__init__()
        self.quick = quick
        self.c4_only = c4_only
        self.media_high = media_high
        self.report = {"scope": __doc__, "commands": [], "artifacts": self.artifacts, "cases": [], "quick": quick,
                       "mediaHigh": media_high, "c4Only": c4_only}

    def prepare(self):
        super().prepare()
        for name in ("nademodes", "goldcraft_nademodes_test"):
            self.copy(ROOT / "build/amxx/plugins" / (name + ".amxx"), base.AMXX / "plugins" / (name + ".amxx"))
        lines = (base.AMXX / "configs/plugins.ini").read_text().splitlines()
        lines = [line for line in lines if "goldcraft_zp_reapi_test" not in line]
        first = "nademodes.amxx debug\n"
        if self.media_high:
            name = "goldcraft_nademodes_media_test.amxx"
            self.copy(ROOT / "build/amxx/plugins" / name, base.AMXX / "plugins" / name)
            first = name + " debug\n" + first
        self.put(base.AMXX / "configs/plugins.ini", (first + "\n".join(lines) + "\ngoldcraft_nademodes_test.amxx debug\n").encode())
        for name in ("nademodes.txt", "nademodes_goldcraft.txt"):
            self.copy(ROOT / "amxx/nade_modes/lang" / name, base.AMXX / "data/lang" / name)
        self.copy(ROOT / "amxx/nade_modes/configs/nade_modes.cfg", base.AMXX / "configs/nade_modes.cfg")
        self.put(base.GAME / "cstrike/goldcraft_nademodes_test.cfg", b"""exec goldcraft_zp_reapi.cfg
goldcraft_nademodes_test 1
nademodes_bot_support 0
nademodes_limit_system 0
nademodes_effects 1
nademodes_affect_owner 0
nademodes_team_play 1
nademodes_remove_if_player_dies 1
nademodes_trip_grenade_arm_time 0.1
nademodes_proximity_arm_time 0.1
nademodes_motion_arm_time 0.1
nademodes_satchel_arm_time 0.1
nademodes_grenade_react 1
nademodes_flash_react 1
nademodes_smoke_react 1
zp_gamemode_delay 100000
zp_nemesis_chance 0
zp_survivor_chance 0
zp_swarm_chance 0
zp_plague_chance 0
zp_armageddon_chance 0
""")
        # Register this generated report for restoration as well.
        self.put(base.AMXX / "logs/goldcraft-nademodes.jsonl", b"")
        for directory in (base.GAME, base.GAME / "cstrike"):
            self.put(directory / "qconsole.log", b"")

    def command(self, text):
        response = super().command(text)
        if text.startswith("gc_nm_"):
            for line in response.splitlines():
                if "[GC NM] FAIL" in line:
                    print(line, flush=True)
        return response

    def ready(self):
        deadline = time.monotonic() + 40
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f"Fixture exited {self.process.returncode}")
            try:
                if "ReHLDS" in self.command("version"):
                    return
            except (OSError, RuntimeError):
                pass
            self.wait(0.3)
        raise TimeoutError("Fixture startup timeout")

    def run(self):
        self.prepare()
        env = {key: value for key, value in os.environ.items() if not key.startswith("GOLDCRAFT_")}
        LOG.parent.mkdir(parents=True, exist_ok=True)
        # Give the dedicated server a real, hidden Windows console. Redirecting
        # inherited stdin into a CREATE_NO_WINDOW process is not a console handle
        # and triggers CTextConsoleWin32::GetLine failures in this engine.
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        self.process = subprocess.Popen([
            str(base.GAME / "hlds.exe"), "-game", "cstrike", "-insecure", "-nomaster", "-console", "-condebug",
            *(["-goldcraft_precache_test"] if self.media_high else []),
            "+ip", "127.0.0.1", "-port", str(self.config["csPort"]), "-maxplayers", "4",
            "+sv_rcon_condebug", "0", "+sv_lan", "1", "+map", "cs_assault", "+exec", "goldcraft_nademodes_test.cfg"],
            cwd=base.GAME, env=env, startupinfo=startup, creationflags=subprocess.CREATE_NEW_CONSOLE)
        self.report["pid"] = self.process.pid
        print(f"Independent Nade Modes fixture PID {self.process.pid}", flush=True)
        self.ready()
        self.wait(1.0)
        self.report["plugins"] = self.command("amxx plugins")
        self.report["modules"] = self.command("amxx modules")
        if "bad load" in self.report["plugins"] or "NadeModes" not in self.report["plugins"]:
            raise RuntimeError("Nade Modes/fixture plugin did not load")
        self.command("exec goldcraft_nademodes_test.cfg")
        self.command("gc_nm_create")
        self.command("sv_restart 1")
        self.wait(1.7)
        self.command("gc_nm_start")
        cases = [(mode, race, 0) for race in range(3) for mode in range(7)]
        cases += [(mode, 0, 1) for mode in range(7)]
        if self.quick:
            cases = [(5, 0, 0), (5, 2, 0), (3, 1, 0), (5, 0, 1)]
        if self.c4_only:
            cases = []
        for mode, race, infection in cases:
            self.command(f"gc_nm_begin {mode} {race} {infection}")
            self.wait(0.38)
            self.command("gc_nm_trigger")
            self.wait(3.0 if mode == 6 and race == 2 else 1.35)
            self.command("gc_nm_finish")
            self.report["cases"].append({"mode": mode, "race": race, "infection": infection})
            print(f"Nade Modes progress: {len(self.report['cases'])}/{len(cases)} cases", flush=True)
        self.command("nademodes_status")
        if not self.c4_only:
            self.command("gc_nm_beam")
            self.wait(0.75)
            self.command("gc_nm_beam_check")
            self.command("gc_nm_extra")
            self.command("gc_nm_team_hold 1 0 0")
            self.wait(0.4)
            self.command("gc_nm_team_release")
            self.wait(1.35)
            self.command("gc_nm_finish")
            self.command("gc_nm_begin 6 0 0")
            self.wait(0.38)
            self.command("gc_nm_homing_team")
            self.command("gc_nm_more_lifecycle")
            self.command("gc_nm_delayed")
            self.wait(0.75)
            self.command("gc_nm_reuse")
            self.command("gc_nm_media " + str(int(self.media_high)))
        self.command("gc_nm_lifecycle")
        self.command("sv_restart 1")
        self.wait(1.65)
        self.command("gc_nm_after_round")
        self.command("gc_nm_start")
        self.command("gc_nm_lifecycle")
        self.command("gc_nm_remove_player")
        self.wait(0.65)
        self.command("gc_nm_after_disconnect")
        if not self.c4_only:
            self.command("gc_nm_before_map")
            self.command("changelevel cs_assault")
            self.wait(1.5)
            self.ready()
            self.command("exec goldcraft_nademodes_test.cfg")
            self.command("gc_nm_after_map")
            self.report["mapReloadKeptServerProcess"] = self.process.poll() is None
            saved = (base.AMXX / "configs/nade_modes.cfg").read_text(encoding="utf-8-sig")
            self.report["savedConfigBytes"] = len(saved.encode("utf-8"))
            self.report["savedConfigSettings"] = {name: float(value) for name, value in
                re.findall(r"(?m)^\s*(nademodes_trip_scan_interval|nademodes_homing_velocity_deviation)\s+([0-9.]+)", saved)}
            self.report["configurationPersisted"] = self.report["savedConfigSettings"] == {
                "nademodes_trip_scan_interval": 0.03125, "nademodes_homing_velocity_deviation": 55.125}
        self.report["checks"] = [json.loads(line) for line in (base.AMXX / "logs/goldcraft-nademodes.jsonl").read_text().splitlines() if line]
        errors = {}
        for path in (base.AMXX / "logs").glob("error_*.log"):
            extra = path.read_bytes()[self.error_offsets.get(path, 0):].decode("utf-8", errors="replace")
            if extra.strip(): errors[path.name] = extra
        self.report["runtimeErrors"] = errors
        checks = self.report["checks"]
        expected = 8 if self.c4_only else 50 if self.quick else 200
        self.report["passed"] = (len(checks) >= expected and all(check["passed"] for check in checks) and not errors
                                 and (self.c4_only or self.report.get("configurationPersisted", False)))

    def close(self):
        if self.process is not None and self.process.poll() is None:
            try: self.command("quit")
            except (OSError, RuntimeError): pass
            try: self.process.wait(5)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                self.process.wait(5)
        checks = base.AMXX / "logs/goldcraft-nademodes.jsonl"
        if checks.exists():
            self.report["checks"] = [json.loads(line) for line in checks.read_text().splitlines() if line]
        self.report["runtimeErrors"] = {}
        for path in (base.AMXX / "logs").glob("error_*.log"):
            extra = path.read_bytes()[self.error_offsets.get(path, 0):].decode("utf-8", errors="replace")
            if extra.strip(): self.report["runtimeErrors"][path.name] = extra
        if self.report["runtimeErrors"]:
            self.report["passed"] = False
        console = "\n".join(path.read_text(errors="replace") for path in
            (base.GAME / "qconsole.log", base.GAME / "cstrike/qconsole.log") if path.exists())
        console = console.replace(self.config["csRconToken"], "[redacted]")
        LOG.write_text(console, encoding="utf-8")
        for path, data in reversed(list(self.before.items())):
            base.safe(path)
            if data is None: path.unlink(missing_ok=True)
            else: path.write_bytes(data)
        self.report["fixtureFilesRestored"] = all((not path.exists()) if data is None else path.read_bytes() == data for path, data in self.before.items())
        self.report["fixtureStopped"] = self.process is None or self.process.poll() is not None
        self.report["passed"] = (self.report.get("passed", False) and self.report["fixtureFilesRestored"]
                                 and self.report["fixtureStopped"] and not self.report["runtimeErrors"])
        output = ROOT / "analysis/goldcraft-tests" / ("nademodes-" + STAMP + ".json")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(self.report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"report": output.relative_to(ROOT).as_posix(), "passed": self.report.get("passed", False),
                          "checks": len(self.report.get("checks", [])), "restored": self.report["fixtureFilesRestored"],
                          "stopped": self.report["fixtureStopped"]}), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--debugger", type=Path, help="Optional installed CDB; capture only the owned failed fixture")
    parser.add_argument("--c4-only", action="store_true", help="Diagnostic reproduction only; not full acceptance")
    parser.add_argument("--media-high", action="store_true", help="Reserve sparse slots before actual grenade media precache; server-side validation only")
    args = parser.parse_args()
    run = Run(args.quick, args.c4_only, args.media_high)
    try:
        run.run()
    except Exception as error:
        run.report["error"] = str(error)
        run.report["passed"] = False
        print(type(error).__name__ + ": " + str(error), flush=True)
        if args.debugger and run.process is not None and run.process.poll() is None:
            try:
                stack = ROOT / "analysis/goldcraft-tests" / ("nademodes-" + STAMP + "-stacks.txt")
                debug_env = dict(os.environ, _NT_SYMBOL_PATH=str(ROOT / "build/regamedll/Release") + ";" + str(ROOT / "build/rehlds/Release"))
                result = subprocess.run([str(args.debugger.resolve()), "-pv", "-p", str(run.process.pid),
                    "-y", debug_env["_NT_SYMBOL_PATH"], "-c", "~0s; kb 40; lm; q"],
                    capture_output=True, text=True, timeout=20, env=debug_env, creationflags=subprocess.CREATE_NO_WINDOW)
                stack.write_text((result.stdout + result.stderr).replace(run.config["csRconToken"], "[redacted]"), encoding="utf-8")
                run.report["failureStacks"] = stack.relative_to(ROOT).as_posix()
            except (OSError, subprocess.TimeoutExpired) as debug_error:
                run.report["debuggerError"] = str(debug_error)
                if isinstance(debug_error, subprocess.TimeoutExpired) and debug_error.stdout:
                    partial = debug_error.stdout.decode(errors="replace") if isinstance(debug_error.stdout, bytes) else debug_error.stdout
                    stack.write_text(partial, encoding="utf-8")
                    run.report["failureStacks"] = stack.relative_to(ROOT).as_posix()
    finally:
        run.close()
    sys.exit(0 if run.report.get("passed") else 1)
