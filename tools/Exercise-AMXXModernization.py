"""Real native-server checks for modern Pawn APIs, menu lifecycle and INI data.

Registered commands are driven by AMXX amxclient_cmd; positive menu choices
use explicitly controlled calls to the production callback with copied item
data. A test-only build of the production buy-menu source disables Bot
auto-buy to reach the human menu branch. This is not network menuselect,
graphical input, or visual acceptance. Production artifacts are tested in
the separate ZombieReAPI/NadeModes regressions without this substitution.
The existing independent fixture is restored and its owned process stopped.
"""
from pathlib import Path
import importlib.util
import json
import os
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("modern_base", ROOT / "tools/Exercise-ZombieReAPI.py")
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
STAMP = str(time.time_ns())
LOG = ROOT / "build/logs/amxx-modernization-fixture.stdout.log"


class Run(base.Run):
    def prepare(self):
        super().prepare()
        for name in ("admin", "nademodes", "goldcraft", "goldcraft_modern_test"):
            self.copy(ROOT / "build/amxx/plugins" / (name + ".amxx"), base.AMXX / "plugins" / (name + ".amxx"))
        self.copy(ROOT / "build/amxx/plugins/goldcraft_buy_menu_fixture.amxx",
                  base.AMXX / "plugins/zp50_buy_menus.amxx")
        self.report["buyMenuFixture"] = {
            "source": "amxx/tests/goldcraft_buy_menu_fixture.sma",
            "override": "Only is_user_bot calls in the included buy-menu source return false.",
            "deployedToMain": False,
        }
        self.copy(base.LIVE / "cstrike/addons/amxmodx/modules/goldcraft_amxx.dll",
                  base.AMXX / "modules/goldcraft_amxx.dll")
        lines = (base.AMXX / "configs/plugins.ini").read_text().splitlines()
        lines = [line for line in lines if "goldcraft_zp_reapi_test" not in line]
        self.put(base.AMXX / "configs/plugins.ini", ("admin.amxx debug\nnademodes.amxx debug\ngoldcraft.amxx debug\n" +
                 "\n".join(lines) + "\ngoldcraft_modern_test.amxx debug\n").encode())
        self.put(base.AMXX / "configs/modules.ini", b"goldcraft\nreapi\nfakemeta\nhamsandwich\n")
        for name in ("admin.txt", "common.txt"):
            self.copy(ROOT / "amxx/administration/lang" / name, base.AMXX / "data/lang" / name)
        self.put(base.AMXX / "configs/users.ini",
                 b'; Synthetic records for database tests, not Steam authentication.\n'
                 b'"STEAM_0:0:0" "" "abcdefghijklmnopqrstuv" "ce"\n')
        for name in ("nademodes.txt", "nademodes_goldcraft.txt"):
            self.copy(ROOT / "amxx/nade_modes/lang" / name, base.AMXX / "data/lang" / name)
        self.copy(ROOT / "amxx/nade_modes/configs/nade_modes.cfg", base.AMXX / "configs/nade_modes.cfg")
        self.put(base.AMXX / "configs/gc-modern-test.ini", b"")
        self.put(base.AMXX / "logs/goldcraft-modern.jsonl", b"")
        self.put(base.GAME / "cstrike/goldcraft_modern.cfg", b"""exec goldcraft_zp_reapi.cfg
goldcraft_modern_test 1
mc_allow_switch 1
zp_gamemode_delay 100000
zp_prevent_consecutive_modes 0
zp_random_primary 0
zp_random_secondary 0
zp_random_grenades 0
zp_buy_custom_time 180
zp_buy_custom_primary 1
zp_buy_custom_secondary 1
zp_buy_custom_grenades 0
nademodes_bot_support 0
""")
        for directory in (base.GAME, base.GAME / "cstrike"):
            self.put(directory / "qconsole.log", b"")
        self.report.update(scope=__doc__, variants=[])

    def command(self, text):
        response = super().command(text)
        for line in response.splitlines():
            if "[GC Modern]" in line:
                print(line, flush=True)
        return response

    def run(self):
        self.prepare()
        env = {key: value for key, value in os.environ.items() if not key.startswith("GOLDCRAFT_")}
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        self.process = subprocess.Popen([
            str(base.GAME / "hlds.exe"), "-game", "cstrike", "-insecure", "-nomaster", "-console", "-condebug",
            "+ip", "127.0.0.1", "-port", str(self.config["csPort"]), "-maxplayers", "4",
            "+sv_rcon_condebug", "0", "+sv_lan", "1", "+map", "cs_assault", "+exec", "goldcraft_modern.cfg"],
            cwd=base.GAME, env=env, startupinfo=startup, creationflags=subprocess.CREATE_NEW_CONSOLE)
        self.report["pid"] = self.process.pid
        print(f"Independent AMXX modernization fixture PID {self.process.pid}", flush=True)
        self.ready()
        self.wait(0.7)
        self.report["plugins"] = self.command("amxx plugins")
        self.report["modules"] = self.command("amxx modules")
        if "bad load" in self.report["plugins"]:
            raise RuntimeError("Modernization test plugin failed to load")
        self.command("exec goldcraft_modern.cfg")
        self.report["enabled"] = self.command("goldcraft_modern_test")
        self.command("gc_modern_start")
        self.wait(0.4)
        self.command("gc_modern_admin 0")
        for _ in range(2):
            self.command('amx_addadmin "STEAM_0:1:0" "l" "" "steamid"')
        self.command("gc_modern_admin 1")
        self.command("amx_reloadadmins")
        self.command("gc_modern_admin 2")
        self.command("gc_modern_menus")
        self.wait(1.2)
        self.command("gc_modern_timeout")
        self.command("gc_modern_disconnect")
        self.wait(0.4)
        self.command("gc_modern_reconnect")
        self.wait(0.4)
        self.command("sv_restart 1")
        self.wait(2.0)
        self.command("gc_modern_round")
        self.report["passed"] = True

    def close(self):
        if self.process is not None and self.process.poll() is None:
            try:
                self.command("quit")
            except (OSError, RuntimeError):
                pass
            try:
                self.process.wait(5)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                self.process.wait(5)
        report = base.AMXX / "logs/goldcraft-modern.jsonl"
        self.report["checks"] = [json.loads(line) for line in report.read_text().splitlines() if line] if report.exists() else []
        self.report["runtimeErrors"] = {}
        for path in (base.AMXX / "logs").glob("error_*.log"):
            extra = path.read_bytes()[getattr(self, "error_offsets", {}).get(path, 0):].decode("utf-8", errors="replace")
            if extra.strip():
                self.report["runtimeErrors"][path.name] = extra
        console = "\n".join(path.read_text(errors="replace") for path in
            (base.GAME / "qconsole.log", base.GAME / "cstrike/qconsole.log") if path.exists())
        self.report["missingMenuFrontEndWarning"] = "Menus Front-End plugin itself is not loaded" in console
        LOG.parent.mkdir(parents=True, exist_ok=True)
        LOG.write_text(console.replace(self.config["csRconToken"], "[redacted]"), encoding="utf-8")
        for path, data in reversed(list(self.before.items())):
            base.safe(path)
            if data is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(data)
        self.report["fixtureFilesRestored"] = all(not p.exists() if data is None else p.read_bytes() == data for p, data in self.before.items())
        self.report["fixtureStopped"] = self.process is None or self.process.poll() is not None
        checks = self.report["checks"]
        self.report["passed"] = bool(self.report.get("passed") and len(checks) >= 70 and all(c["passed"] for c in checks)
                                    and not self.report["runtimeErrors"] and not self.report["missingMenuFrontEndWarning"]
                                    and self.report["fixtureFilesRestored"] and self.report["fixtureStopped"])
        output = ROOT / "analysis/goldcraft-tests" / ("amxx-modernization-" + STAMP + ".json")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(self.report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"report": output.relative_to(ROOT).as_posix(), "passed": self.report["passed"],
                          "checks": len(checks), "failed": [c["name"] for c in checks if not c["passed"]],
                          "restored": self.report["fixtureFilesRestored"], "stopped": self.report["fixtureStopped"]}), flush=True)


if __name__ == "__main__":
    run = Run()
    try:
        run.run()
    except Exception as error:
        run.report["error"] = str(error)
        run.report["passed"] = False
        print(type(error).__name__ + ": " + str(error), flush=True)
    finally:
        run.close()
    sys.exit(0 if run.report["passed"] else 1)
