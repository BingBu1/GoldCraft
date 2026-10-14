"""Test the not-in-game slot-26 callback and connected ZP Rage cleanup.

Uses the inactive headless fixture, 32 slots and sv_lan 0. Optional original
bytecode is a negative control, not deployed to the ordinary sandbox server.
The aborted-connection state is a direct production callback adapter, not a
real Steam network abort. Connected kicks use the actual engine lifecycle.
All changed fixture files are restored; no graphical client or Java is started.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("zp_fixture", ROOT / "tools/Exercise-ZombieReAPI.py")
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)


class Run(base.Run):
    def __init__(self, output, baseline=None):
        super().__init__()
        self.output = output
        self.baseline = baseline
        self.console = output.with_suffix(".stdout.log")
        self.report["scope"] = __doc__
        self.report["negativeControl"] = baseline is not None

    def run(self):
        self.prepare()
        # Test the release replacement against the installed plugin set, not
        # other locally rebuilt bytecode that has not been deployed.
        names = re.findall(r"^([a-z0-9_]+)\.amxx", (base.AMXX / "configs/plugins.ini").read_text(), re.M)
        for name in names:
            if not name.startswith("goldcraft_"):
                self.copy(base.LIVE / "cstrike/addons/amxmodx/plugins" / (name + ".amxx"),
                          base.AMXX / "plugins" / (name + ".amxx"))
        self.copy(base.OUT / "plugins/zp50_class_zombie_rage.amxx",
                  base.AMXX / "plugins/zp50_class_zombie_rage.amxx")
        self.copy(base.OUT / "plugins/goldcraft_zp_disconnect_test.amxx",
                  base.AMXX / "plugins/goldcraft_zp_disconnect_test.amxx")
        if self.baseline:
            self.copy(self.baseline, base.AMXX / "plugins/zp50_class_zombie_rage.amxx")
        lists = (base.AMXX / "configs/plugins.ini").read_bytes()
        self.put(base.AMXX / "configs/plugins.ini", lists + b"goldcraft_zp_disconnect_test.amxx debug\n")
        self.put(base.GAME / "cstrike/server.cfg",
                 ('sv_lan 0\nlog off\nrcon_password "' + self.config["csRconToken"] + '"\n').encode())
        self.put(base.GAME / "cstrike/goldcraft_zp_reapi.cfg",
                 (base.GAME / "cstrike/goldcraft_zp_reapi.cfg").read_bytes()
                 + b"\nzp_gamemode_delay 100000\n")
        self.put(base.AMXX / "logs/goldcraft-zp-disconnect.jsonl", b"")
        for directory in (base.GAME, base.GAME / "cstrike"):
            self.put(directory / "qconsole.log", b"")
        base.FIXTURE_LOG = base.GAME / "qconsole.log"
        self.console.parent.mkdir(parents=True, exist_ok=True)
        env = {k: v for k, v in os.environ.items() if not k.startswith("GOLDCRAFT_")}
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        self.process = subprocess.Popen([
            str(base.GAME / "hlds.exe"), "-game", "cstrike", "-insecure", "-nomaster", "-console", "-condebug",
            "+ip", "127.0.0.1", "-port", str(self.config["csPort"]), "-maxplayers", "32",
            "+sv_lan", "0", "+map", "cs_assault", "+exec", "goldcraft_zp_reapi.cfg"],
            cwd=base.GAME, env=env, startupinfo=startup,
            creationflags=subprocess.CREATE_NEW_CONSOLE)
        self.report["pid"] = self.process.pid
        self.ready()
        self.wait(0.5)
        self.report["plugins"] = self.command("amxx plugins")
        self.report["modules"] = self.command("amxx modules")
        if "bad load" in self.report["plugins"]:
            raise RuntimeError("Plugin load failed")
        self.report["lan"] = self.command("sv_lan")
        if not re.search(r'"sv_lan"\s+is\s+"0"', self.report["lan"]):
            raise RuntimeError("Fixture must use sv_lan 0")
        self.command("gc_zp_disconnect_abort")
        self.command("gc_zp_disconnect_live")
        self.report["checks"] = [json.loads(line) for line in
            (base.AMXX / "logs/goldcraft-zp-disconnect.jsonl").read_text().splitlines() if line]

    def close(self):
        if self.process is not None and self.process.poll() is None:
            try:
                self.command("quit")
                self.process.wait(5)
                self.report["normalExit"] = self.process.returncode == 0
            except (OSError, RuntimeError, subprocess.TimeoutExpired):
                self.report["normalExit"] = False
        errors = {}
        for path in (base.AMXX / "logs").glob("error_*.log"):
            extra = path.read_bytes()[getattr(self, "error_offsets", {}).get(path, 0):].decode("utf-8", errors="replace")
            if extra.strip():
                errors[path.name] = extra
        self.report["runtimeErrors"] = errors
        console = "\n".join(path.read_text(errors="replace") for path in
            (base.GAME / "qconsole.log", base.GAME / "cstrike/qconsole.log") if path.exists())
        self.console.write_text(console.replace(self.config["csRconToken"], "[redacted]"), encoding="utf-8")
        self.report["engineFatal"] = bool(re.search(r"FATAL ERROR|Host_Error:|Sys_Error:", console))
        text = "\n".join(errors.values())
        self.report["invalidPlayer26Count"] = text.count("[ZP] Invalid Player (26)")
        expected_errors = (self.report["invalidPlayer26Count"] == 1
                           and "zp50_class_zombie_rage.amxx" in text
                           and "zp_class_zombie_get_current" in text
                           and len(re.findall(r"Run time error", text)) == 1) if self.baseline else not errors
        checks = self.report.get("checks", [])
        self.report["passed"] = bool(len(checks) == 15 and all(c["passed"] for c in checks)
                                     and expected_errors and not self.report.get("error") and self.report.get("normalExit")
                                     and not self.report["engineFatal"])
        self.report["elapsedSeconds"] = time.monotonic() - self.start_clock
        super().close(self.output)
        self.report["passed"] = bool(self.report["passed"] and self.report["fixtureFilesRestored"]
                                     and self.report["fixtureStopped"])
        self.output.write_text(json.dumps(self.report, indent=2) + "\n", encoding="utf-8")


def safe_output(path):
    path = path.absolute()
    if not path.resolve().is_relative_to(ROOT / "analysis"):
        raise ValueError("Evidence must remain in workspace analysis")
    for part in (path, *path.parents):
        if part.is_symlink() or part.is_junction():
            raise ValueError("Reparse point in evidence path")
        if part == ROOT:
            break
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, help="Verified original Rage bytecode for a negative control")
    args = parser.parse_args()
    output = safe_output(args.output)
    baseline = safe_output(args.baseline) if args.baseline else None
    run = Run(output, baseline)
    run.start_clock = time.monotonic()
    try:
        run.run()
    except BaseException as exc:
        run.report["error"] = str(exc)
        raise
    finally:
        run.close()
    print(json.dumps({"passed": run.report["passed"], "checks": len(run.report.get("checks", [])),
                      "negativeControl": baseline is not None, "seconds": run.report["elapsedSeconds"]}))
    return 0 if run.report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
