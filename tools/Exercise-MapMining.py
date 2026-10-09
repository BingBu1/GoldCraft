"""Actual HLDS mining cvars and entity damage through the authenticated bridge.

Uses one real CBasePlayer fake client and an explicit authority-peer fixture.
This is not a Minecraft JVM, graphical-input, or excavated-world acceptance test.
The inactive independent server is temporarily updated, run with sv_lan 0,
then stopped; every file changed by deployment is restored. Main server stays off.
"""
from pathlib import Path
import importlib.util
import json
import math
import os
import re
import socket
import struct
import subprocess
import sys
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("mining_fixture", ROOT / "tools/Exercise-ZombieReAPI.py")
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
VERSION = int(re.search(r"protocol_version = (\d+)", (ROOT / "native/include/goldcraft/wire.hpp").read_text())[1])
HEADER = struct.Struct("<IHHIIQQ")
STAMP = str(time.time_ns())
LOG = ROOT / "analysis/world-carving/mining-runtime.stdout.log"


class Peer:
    def __init__(self, config):
        self.config = config
        self.socket = socket.create_connection(("127.0.0.1", config["serverPort"]), timeout=3)
        self.socket.settimeout(.05)
        self.lock = threading.RLock()
        self.stop = False
        self.error = None
        self.connection = self.sequence = self.received = self.pose_sequence = 0
        self.world = 0
        self.policy = None
        self.actors = {}
        self.messages = []
        self.pose = None
        self.buffer = b""
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.send(1, bytes.fromhex(config["serverSession"]) + bytes.fromhex(config["serverToken"]) + struct.pack("<II", 4, 0))
        self.thread.start()

    def send(self, kind, body):
        with self.lock:
            self.sequence += 1
            self.socket.sendall(HEADER.pack(0x31464347, VERSION, kind, len(body), 0, self.connection, self.sequence) + body)

    def run(self):
        heartbeat = pose_at = 0
        try:
            while not self.stop:
                now = time.monotonic()
                if self.connection and now >= heartbeat:
                    self.send(3, b""); heartbeat = now + .5
                if self.connection and self.pose and now >= pose_at:
                    with self.lock:
                        head, tail = self.pose
                        self.pose_sequence += 1
                        self.send(50, struct.pack("<QQ", self.world, self.pose_sequence) + head + tail)
                    pose_at = now + .1
                try:
                    data = self.socket.recv(65536)
                except socket.timeout:
                    continue
                if not data:
                    if not self.stop:
                        raise EOFError("Independent host bridge closed")
                    return
                self.buffer += data
                while len(self.buffer) >= HEADER.size:
                    magic, version, kind, size, flags, epoch, sequence = HEADER.unpack_from(self.buffer)
                    if magic != 0x31464347 or version != VERSION or size > 32 * 1024 * 1024 or flags or sequence != self.received + 1:
                        raise ValueError("Invalid host frame")
                    if len(self.buffer) < HEADER.size + size:
                        break
                    body = self.buffer[HEADER.size:HEADER.size + size]
                    self.buffer = self.buffer[HEADER.size + size:]
                    self.received = sequence
                    with self.lock:
                        if kind == 2:
                            self.connection = epoch
                        elif epoch != self.connection:
                            raise ValueError("Unexpected host connection generation")
                        elif kind == 10:
                            self.world = struct.unpack_from("<Q", body)[0]
                        elif kind == 56:
                            self.policy = struct.unpack("<QQII", body)
                        elif kind == 11:
                            self.frozen = struct.unpack_from("<I", body, 20)[0] != 0
                            count = struct.unpack_from("<I", body, 24)[0]
                            if len(body) != 28 + count * 116:
                                raise ValueError("Actor snapshot length")
                            self.actors = {}
                            for index in range(count):
                                offset = 28 + index * 116
                                slot, serial, userid, team, flags, life = struct.unpack_from("<6I", body, offset)
                                pitch, yaw = struct.unpack_from("<2f", body, offset + 56)
                                self.actors[slot] = dict(slot=slot, serial=serial, userid=userid, team=team, flags=flags, life=life, pitch=pitch, yaw=yaw)
                        elif kind in (14, 41, 58):
                            self.messages.append((kind, body))
        except Exception as exc:
            if not self.stop:
                self.error = exc

    def take(self, kind, predicate=lambda body: True):
        with self.lock:
            for index, (current, body) in enumerate(self.messages):
                if kind == current and predicate(body):
                    self.messages.pop(index)
                    return body
        return None

    def close(self):
        self.stop = True
        self.thread.join(2)
        self.socket.close()
        if self.thread.is_alive():
            raise RuntimeError("Authority fixture thread did not stop")


class Run(base.Run):
    def __init__(self):
        super().__init__()
        self.peer = None
        self.event = 0
        self.uuid = uuid.uuid4().bytes
        self.bindings = base.TEST / "mining-bindings.bin"
        self.report = {"scope": __doc__, "commands": [], "artifacts": self.artifacts, "checks": {}, "protocol": VERSION}

    def check(self, name, passed):
        self.report["checks"][name] = bool(passed)
        print(json.dumps({"check": name, "passed": bool(passed)}), flush=True)
        if not passed:
            raise AssertionError(name)

    def until(self, predicate, label, seconds=12):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if self.process and self.process.poll() is not None:
                raise RuntimeError("Independent HLDS exited: " + str(self.process.returncode))
            if self.peer and self.peer.error:
                raise self.peer.error
            try:
                result = predicate()
                if result:
                    return result
            except (FileNotFoundError, PermissionError, json.JSONDecodeError, KeyError, StopIteration):
                pass
            time.sleep(.03)
        raise TimeoutError(label)

    def prepare(self):
        if not os.isatty(0):
            raise RuntimeError("Run in a console/PTY: ReHLDS requires a real console input handle")
        if any(str(p.get("ExecutablePath", "")).lower() == str(base.GAME / "hlds.exe").lower() for p in base.existing_processes()):
            raise RuntimeError("The independent fixture is already running")
        for name, kind in (("csPort", socket.SOCK_DGRAM), ("serverPort", socket.SOCK_STREAM)):
            with socket.socket(socket.AF_INET, kind) as sock:
                sock.bind(("127.0.0.1", self.config[name]))
        for name in ("hlds.exe", "swds.dll", "filesystem_stdio.dll", "steam_api.dll"):
            self.copy(ROOT / "dist/rehlds" / name, base.GAME / name)
        self.copy(ROOT / "build/regamedll-headless/Release/mp.dll", base.GAME / "cstrike/dlls/mp.dll")
        self.copy(ROOT / "build/native-x86/Release/goldcraft_amxx.dll", base.AMXX / "modules/goldcraft_amxx.dll")
        for name in ("reapi", "fakemeta", "hamsandwich"):
            self.copy(base.LIVE / f"cstrike/addons/amxmodx/modules/{name}_amxx.dll", base.AMXX / f"modules/{name}_amxx.dll")
        plugins = ("goldcraft", "goldcraft_headless_test", "goldcraft_map_mining_test")
        for name in plugins:
            self.copy(ROOT / f"build/amxx/plugins/{name}.amxx", base.AMXX / f"plugins/{name}.amxx")
        self.put(base.AMXX / "configs/plugins.ini", "".join(name + ".amxx debug\n" for name in plugins).encode())
        self.put(base.AMXX / "configs/modules.ini", b"goldcraft\nreapi\nfakemeta\nhamsandwich\n")
        # No change to the master configuration or the primary server's population.
        # Host_InitializeGameDLL drains autoexec/command-line text before loading
        # the game DLL. ReGameDLL's game_init.cfg is the supported early stage.
        self.put(base.GAME / "cstrike/autoexec.cfg", b"// Independent mining fixture.\n")
        self.put(base.GAME / "cstrike/game_init.cfg", b"mc_map_mining 0\n")
        self.put(base.GAME / "cstrike/server.cfg", ("\n".join([
            "sv_lan 0", "log off", "mp_freezetime 0", "mp_round_infinite 1", "mp_autoteambalance 0", "mp_limitteams 0",
            "mp_autokick 0", "mp_respawn_immunitytime 0", "mc_default_form 0", "mc_allow_switch 0",
            "goldcraft_headless_test 1", "mc_map_mining_test 1", 'rcon_password "' + self.config["csRconToken"] + '"']) + "\n").encode())
        self.before[self.bindings] = self.bindings.read_bytes() if self.bindings.exists() else None
        self.error_offsets = {p: p.stat().st_size for p in (base.AMXX / "logs").glob("error_*.log")}

    def state(self):
        path = base.TEST / "logs/goldcraft-mining-status.json"
        if path.stat().st_mtime < self.started:
            raise ValueError("Stale native status")
        return json.loads(path.read_text())

    def start_native(self):
        self.prepare()
        env = {k: v for k, v in os.environ.items() if not k.startswith("GOLDCRAFT_")}
        for field in ("PORT", "SESSION", "TOKEN"):
            env["GOLDCRAFT_SERVER_" + field] = str(self.config["server" + field.title()])
        env["GOLDCRAFT_HEADLESS_BINDINGS"] = str(self.bindings)
        env["GOLDCRAFT_SERVER_STATUS"] = str(base.TEST / "logs/goldcraft-mining-status.json")
        self.log = LOG.open("w", encoding="utf-8")
        self.process = subprocess.Popen([str(base.GAME / "hlds.exe"), "-game", "cstrike", "-insecure", "-nomaster", "-console",
                                        "+ip", "127.0.0.1", "-port", str(self.config["csPort"]), "-maxplayers", "4",
                                        "+sv_lan", "0", "+map", "cs_assault"], cwd=base.GAME, env=env,
                                       stdout=self.log, stderr=subprocess.STDOUT, stdin=None, creationflags=0)
        self.report["pid"] = self.process.pid
        print(json.dumps({"independentServer": self.process.pid, "mainServerStarted": False}), flush=True)

    def start(self):
        self.start_native()
        def ready():
            try:
                self.peer = Peer(self.config)
                return True
            except OSError:
                return False
        self.until(ready, "Native bridge start")
        self.until(lambda: self.peer.policy, "Initial host policy")
        self.report["initialPolicy"] = self.peer.policy
        self.report["startupCvar"] = self.command("mc_map_mining")
        self.check("game_init.cfg sees mc_map_mining before map activation", self.peer.policy[2] == 0
                   and '"mc_map_mining" is "0"' in self.report["startupCvar"])
        self.check("server is not LAN", bool(re.search(r'"sv_lan" is "0"', self.command("sv_lan"))))
        self.check("server.cfg applies renamed default form cvar", '"mc_default_form" is "0"' in self.command("mc_default_form"))
        self.check("server.cfg applies renamed switch cvar", '"mc_allow_switch" is "0"' in self.command("mc_allow_switch"))
        self.command("mc_default_form 1")
        self.command("mc_allow_switch 1")
        self.report["modules"] = self.command("amxx modules")
        self.report["plugins"] = self.command("amxx plugins")
        answer = self.command("gc_headless_create")
        self.slot = int(re.search(r"slot=(\d+)", answer)[1])
        def binding():
            data = self.bindings.read_bytes()
            if len(data) < 12:
                return None
            epoch, count = struct.unpack_from("<QI", data)
            self.report["bindingShape"] = {"bytes": len(data), "epoch": epoch, "count": count, "peerWorld": self.peer.world}
            if epoch != self.peer.world or len(data) != 12 + count * 24:
                return None
            for n in range(count):
                slot, serial = struct.unpack_from("<II", data, 12 + n * 24)
                if slot == self.slot:
                    return serial, data[20 + n * 24:36 + n * 24]
            return None
        serial, token = self.until(binding, "Private fixture binding")
        self.peer.send(13, struct.pack("<QII", self.peer.world, self.slot, serial) + token + self.uuid)
        response = self.until(lambda: self.peer.take(14), "Real pairing result")
        self.check("authority fixture uses a validated real player binding", struct.unpack_from("<I", response, 16)[0] == 0)
        self.until(lambda: self.peer.actors.get(self.slot, {}).get("flags", 0) & 64, "Minecraft form")
        self.until(lambda: not self.peer.frozen, "Initial native freeze period ends")

    def set_mode(self, mode):
        previous = self.peer.policy
        self.command("mc_map_mining " + str(mode))
        self.until(lambda: self.peer.policy[2] == (mode if mode in (0, 1, 2) else 0) and self.peer.policy != previous, "Live mining policy update")
        return self.peer.policy

    def pose(self, eye, point, reverse=False):
        actor = self.peer.actors[self.slot]
        delta = [b - a for a, b in zip(eye, point)]
        yaw = math.degrees(math.atan2(delta[1], delta[0])) + (180 if reverse else 0)
        pitch = -math.degrees(math.atan2(delta[2], math.hypot(delta[0], delta[1])))
        eye_height = 1.62
        feet = (eye[0] / 32, (eye[2] - eye_height * 32) / 32 + 64, -eye[1] / 32)
        head = struct.pack("<III", self.slot, actor["serial"], actor["life"]) + self.uuid
        tail = struct.pack("<I11f", 1, *feet, 0, 0, 0, pitch, -yaw - 90, eye_height, .6, 1.8)
        with self.peer.lock:
            self.peer.pose = head, tail
        self.until(lambda: self.peer.actors[self.slot]["flags"] & 32 and abs(self.peer.actors[self.slot]["yaw"] - yaw) < .01, "Native movement lease and view")
        # Wait for one more native frame after the actor snapshot.
        time.sleep(.05)

    def target(self, model):
        answer = self.command(f"gc_mining_probe {model} {self.slot}")
        match = re.search(r"target=(\d+) eye=([^ ]+) point=([^ ]+) early=(\d+)", answer)
        if not match:
            raise RuntimeError("No native fixture approach: " + answer)
        self.target_slot = int(match[1])
        self.eye, self.point = [[float(v) for v in match[n].split(",")] for n in (2, 3)]
        self.model = model
        self.until(lambda: any(e["slot"] == self.target_slot for e in self.state()["hostEntities"]), "Target snapshot")
        entity = next(e for e in self.state()["hostEntities"] if e["slot"] == self.target_slot)
        self.target_serial = entity["serial"]
        self.pose(self.eye, self.point)
        self.check("all mc_ server cvars available during AMXX initialization", match[4] == "1")

    def request(self, **overrides):
        self.event += 1
        actor = self.peer.actors[self.slot]
        q = dict(epoch=self.peer.world, revision=self.peer.policy[1], event=self.event, slot=self.slot,
                 serial=actor["serial"], life=actor["life"], uuid=self.uuid, target=self.target_slot,
                 target_serial=self.target_serial, model=self.model, point=self.point, damage=2., reach=192.)
        q.update(overrides)
        payload = struct.pack("<QQQIII", q["epoch"], q["revision"], q["event"], q["slot"], q["serial"], q["life"]) + q["uuid"]
        payload += struct.pack("<III5f", q["target"], q["target_serial"], q["model"], *q["point"], q["damage"], q["reach"])
        self.peer.send(57, payload)
        raw = self.until(lambda: self.peer.take(58, lambda b: struct.unpack_from("<Q", b, 8)[0] == q["event"]), "Mining result")
        values = struct.unpack("<QQQ6I2f", raw)
        result = dict(status=values[8], before=values[9], after=values[10], event=q["event"])
        self.report.setdefault("results", []).append(dict(result, actor=actor, frozen=self.peer.frozen))
        return result

    def rested(self):
        time.sleep(.24)

    def run(self):
        self.start()
        self.target(56)
        self.command("gc_mining_edit 100 1")
        result = self.request()
        self.check("mode 0 rejects map mining before native damage", result["status"] == 1 and "calls=0" in self.command("gc_mining_state"))
        self.set_mode(1)
        result = self.request()
        self.check("mode 1 invokes real glass damage and its native club multiplier", result["status"] == 0 and result["before"] == 100 and result["after"] == 96)
        self.check("native damage retains the paired attacker", f"attacker={self.slot}" in self.command("gc_mining_state"))
        duplicate = self.request(event=result["event"])
        self.check("replayed event cannot damage twice", duplicate["status"] == 4 and "health=96.000" in self.command("gc_mining_state"))
        revision = self.peer.policy[1]
        self.set_mode(0)
        self.check("runtime downgrade rejects previously authorized requests", self.request(revision=revision)["status"] == 2)
        self.set_mode(1)
        self.check("reenabling cannot revive an old policy revision", self.request(revision=revision)["status"] == 2)
        self.rested()
        self.check("reused edict incarnation is rejected", self.request(target_serial=self.target_serial + 1)["status"] == 6)
        self.rested()
        self.check("stale player life is rejected", self.request(life=self.peer.actors[self.slot]["life"] + 1)["status"] == 3)
        self.check("wrong paired UUID is rejected", self.request(uuid=bytes(16))["status"] == 3)
        self.rested()
        self.pose(self.eye, self.point, reverse=True)
        self.check("target behind native view is rejected", self.request()["status"] == 7)
        self.pose(self.eye, self.point)
        self.rested()
        self.check("out of reach target is rejected", self.request(reach=8)["status"] == 7)
        self.rested()
        self.command("gc_mining_filter 1")
        result = self.request()
        self.check("Ham veto preserves entity health", result["status"] == 8 and result["before"] == result["after"])
        self.command("gc_mining_filter 0")
        self.rested()
        self.command("gc_mining_edit 100 0")
        self.check("health alone does not make an invulnerable entity mineable", self.request()["status"] == 6)
        self.command("gc_mining_edit 100 1")
        self.set_mode(2)
        self.rested()
        self.check("mode 2 retains native breakable callbacks", self.request()["status"] == 0)
        self.set_mode(0)
        self.command("gc_mining_bullet " + str(self.slot) + " 5")
        self.check("mode 0 leaves ordinary CS bullet damage enabled", "health=91.000" in self.command("gc_mining_state"))
        self.set_mode(1)
        self.rested()
        self.command("gc_mining_edit 3 1")
        result = self.request()
        self.check("native lethal FALSE is recognized as real breakage", result["status"] == 0 and result["after"] <= 0)
        self.target(67)
        self.rested()
        result = self.request()
        self.check("native unbreakable glass immunity is retained", result["status"] == 8 and result["after"] == result["before"])
        self.target(11)
        self.command("gc_mining_edit 100 0")
        self.rested()
        self.check("ordinary door with health but no damage callback is protected", self.request()["status"] == 6)
        self.set_mode(2)
        self.rested()
        self.check("unfinished geometry path reports unavailable rather than false success", self.request()["status"] == 9 and self.peer.policy[3] == 1)
        self.set_mode(1.5)
        self.check("invalid fractional cvar value disables mining", self.request()["status"] == 1)
        self.report["geometryExcavationImplemented"] = False
        self.report["runtimeErrors"] = {p.name: p.read_bytes()[self.error_offsets.get(p, 0):].decode("utf-8", errors="replace")
                                       for p in (base.AMXX / "logs").glob("error_*.log") if p.stat().st_size > self.error_offsets.get(p, 0)}
        self.check("AMXX/ReAPI fixture has no new runtime errors", not self.report["runtimeErrors"])
        self.report["passed"] = True

    def close(self, output=None):
        try:
            if self.peer:
                self.peer.close()
        finally:
            # The base cleanup stops only this exact process and restores all writes.
            super().close(output=output or ROOT / f"analysis/world-carving/mining-runtime-{STAMP}.json")
        if hasattr(self, "log"):
            self.log.close()
        if LOG.exists():
            text = LOG.read_text(encoding="utf-8", errors="replace")
            for key in ("csRconToken", "serverSession", "serverToken", "rconToken"):
                text = text.replace(str(self.config[key]), "[redacted]")
            LOG.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    run = Run()
    try:
        run.run()
    except Exception as exc:
        run.report["error"] = str(exc)
        run.report["passed"] = False
        raise
    finally:
        run.close()
    sys.exit(0 if run.report["passed"] else 1)
