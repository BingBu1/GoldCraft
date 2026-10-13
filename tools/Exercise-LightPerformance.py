"""Place temporary B-side light fixtures and measure the real local Renderer.

Default: 8 then 32 cumulative glowstone blocks, static-shadow OFF/ON at each
count, followed by the normal 99.5 FPS cap. No camera, input, population or
quality controls are changed. Each capture uses Monitor-LivePerformance.py.
Use --cache-modes 1 for a shorter check of the enabled cache; a single mode
does not report a cache improvement percentage.
Use --cache-modes 1 --face-cull-modes 0 1 to compare Studio shadow face culling
with the same fixture and cache enabled. Its final 99.5 FPS sample uses face
culling 1; every capture restores the original settings, including face 0.
Use --cache-modes 1 --depth-executor-modes 0 1 for the independent opaque depth
executor comparison. It is mutually exclusive with face-cull modes, preserves
face culling, and samples executor 1 at the final normal cap before restoration.
Use --cache-modes 1 --bone-palette-modes 0 1 to compare exact GPU bone-palette
reuse. This preserves both Studio shadow options and restores the original cache mode.

Run only while B remains in active MC gameplay, foreground and stationary.
Evidence and a durable recovery manifest stay in analysis/perf-200fps. Recover
an interrupted run with --cleanup <manifest.json>; an unacknowledged placement
is deliberately left for inspection. Vanilla glowstone has no owner/version
metadata: replacing one with an identical block cannot be distinguished, so
do not edit fixture cells or run another world-edit exercise concurrently.
MC air is not proof of BSP free space or visible illumination. Exported and
submitted light counts and Renderer shadow calls are reported separately.
"""
import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "analysis/perf-200fps"
LOGS = ROOT / "sandbox/cs-client-b/logs"
HOST_STATUS = ROOT / "sandbox/cs-server/logs/goldcraft-server-status.json"
MC_STATUS = ROOT / "sandbox/cs-server/logs/minecraft-server-status.json"
ROLES = {"minecraft": ROOT / "sandbox/cs-server/process-MinecraftServer.json",
         "host": ROOT / "sandbox/cs-server/process-CsServer.json",
         "client": ROOT / "sandbox/cs-client-b/process-CsClient.json",
         "minecraftClient": ROOT / "sandbox/cs-client-b/process-MinecraftClient.json"}
LIGHT_KEYS = ("lights", "sceneLightsSubmitted")
NORMAL_CAPS = {"fpsLimit": 99.5, "fpsOverride": 0, "vsync": 0}
PENDING = {"placing", "placed", "removing", "uncertain"}


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def local_path(path, base):
    resolved = path.resolve()
    if not resolved.is_relative_to(base.resolve()):
        raise ValueError("Path escapes the permitted workspace directory")
    # resolve() alone would accept a junction whose target is also in base.
    for part in (path, *path.parents):
        if part == ROOT.parent:
            break
        if part.is_symlink() or (hasattr(part, "is_junction") and part.is_junction()):
            raise ValueError("Fixture paths must not use links or junctions")
    return resolved


def write_json(path, value):
    local_path(path, OUTPUT)
    temporary = path.with_suffix(path.suffix + ".tmp")
    local_path(temporary, OUTPUT)
    with temporary.open("w", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def fresh(path, maximum_ms=2000):
    # A replaced diagnostic file is a missing observation, never an old success.
    deadline = time.monotonic() + .2
    while True:
        try:
            first = path.stat()
            value = read_json(path)
            second = path.stat()
            age = (time.time() - second.st_mtime) * 1000
            if ((first.st_ino, first.st_size, first.st_mtime_ns) ==
                    (second.st_ino, second.st_size, second.st_mtime_ns) and 0 <= age <= maximum_ms):
                return value
        except (OSError, ValueError):
            pass
        if time.monotonic() >= deadline:
            raise RuntimeError(f"No coherent fresh diagnostic: {path.name}")
        time.sleep(.01)


def live_process(pid):
    """Read a live process identity; no process control is performed here."""
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel.GetProcessTimes.argtypes = (wintypes.HANDLE, *([ctypes.POINTER(wintypes.FILETIME)] * 4))
    kernel.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.QueryFullProcessImageNameW.argtypes = (wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR,
                                                 ctypes.POINTER(wintypes.DWORD))
    handle = kernel.OpenProcess(0x1000 | 0x100000, False, pid)
    if not handle:
        if ctypes.get_last_error() == 87:  # ERROR_INVALID_PARAMETER: no such PID.
            return None
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        times = [wintypes.FILETIME() for _ in range(4)]
        image = ctypes.create_unicode_buffer(32768)
        length = wintypes.DWORD(len(image))
        if not kernel.GetProcessTimes(handle, *(ctypes.byref(item) for item in times)):
            raise ctypes.WinError(ctypes.get_last_error())
        if not kernel.QueryFullProcessImageNameW(handle, 0, image, ctypes.byref(length)):
            raise ctypes.WinError(ctypes.get_last_error())
        birth = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
        wait = kernel.WaitForSingleObject(handle, 0)
        if wait == 0:
            return None
        if wait != 258:  # WAIT_TIMEOUT means still alive.
            raise ctypes.WinError(ctypes.get_last_error())
        return {"pid": pid, "birth100ns": birth, "image": image.value}
    finally:
        kernel.CloseHandle(handle)


def process_identity(record_path):
    """Verify the launcher record against a live Windows process, including birth."""
    record = read_json(record_path)
    expected_image = local_path(Path(record["executable"]), ROOT)
    observed = live_process(record["pid"])
    if observed is None:
        raise RuntimeError("A managed sandbox process is not running")
    birth = observed["birth100ns"]
    expected = datetime.fromisoformat(record["startedUtc"].replace("Z", "+00:00")).timestamp()
    if (Path(observed["image"]).resolve() != expected_image
            or abs(birth / 10_000_000 - 11644473600 - expected) > .01):
        raise RuntimeError("Managed process identity changed or its PID was reused")
    return {"pid": record["pid"], "birth100ns": birth}


def candidates(position, yaw):
    """Bounded, deterministic front grid; GoldSrc +Y is Minecraft -Z."""
    angle = math.radians(yaw)
    forward = (math.cos(angle), -math.sin(angle))
    right = (math.sin(angle), math.cos(angle))
    result = []
    # First rows are spread across the view rather than clustered in one corner.
    for distance in (5, 7, 9, 11, 13, 15, 17, 19):
        for side in (-1, 1, -3, 3, -5, 5, -7, 7):
            cell = [math.floor(position[0] + distance * forward[0] + side * right[0]),
                    math.floor(position[1]) + 3,
                    math.floor(position[2] + distance * forward[1] + side * right[1])]
            if cell not in result:
                result.append(cell)
    return result


class LightSnapshotPending(RuntimeError):
    """A live provider is between updates; only pre-capture settling may wait."""


def light_snapshot(sample):
    native = sample["native"]
    if not native.get("sceneLightsApi"):
        raise RuntimeError("Scene light provider is unavailable")
    if not native.get("sceneLightsFresh"):
        raise LightSnapshotPending("Scene light snapshot is stale")
    values = {key: native.get(key) for key in LIGHT_KEYS}
    if any(type(value) is not int or value < 0 for value in values.values()):
        raise RuntimeError("Missing valid light-count diagnostics")
    if values["lights"] != values["sceneLightsSubmitted"]:
        raise LightSnapshotPending("Exported and submitted light counts have not converged")
    return values


class Exercise:
    def __init__(self, args, manifest_path, manifest):
        self.args, self.path, self.data = args, manifest_path, manifest
        self.monitor = module("light_live_monitor", "Monitor-LivePerformance.py")
        self.renderer = module("light_live_renderer", "Exercise-RendererPerformance.py")
        self.rcon = module("light_rcon", "Minecraft-Command.py")
        self.connection = None
        self.request = 1
        self.anchor = None
        self.escape_latched = False
        self.escape_key_state = None

    def arm_escape(self):
        user = ctypes.WinDLL("user32", use_last_error=True)
        self.escape_key_state = user.GetAsyncKeyState
        self.escape_key_state.argtypes = (ctypes.c_int,)
        self.escape_key_state.restype = ctypes.c_short
        # Read once to consume historical low-bit events, but never ignore an
        # Escape that is currently held when the exercise starts.
        self.escape_latched = bool(self.escape_key_state(0x1B) & 0x8000)

    def check_escape(self):
        if self.escape_key_state is not None and self.escape_key_state(0x1B) & 0x8001:
            self.escape_latched = True
        if self.escape_latched:
            raise InterruptedError("Physical Escape requested stopping the automated exercise")

    def save(self):
        write_json(self.path, self.data)

    def connect(self):
        config = read_json(ROOT / "sandbox/cluster.json")
        props = {}
        for line in (ROOT / "sandbox/neoforge-server/server.properties").read_text(encoding="utf-8").splitlines():
            if line and not line.startswith("#") and "=" in line:
                key, value = line.split("=", 1)
                props[key] = value
        if (props.get("enable-rcon") != "true" or int(props.get("rcon.port", 0)) != config["minecraftRconPort"]
                or props.get("rcon.password") != config["rconToken"]):
            raise RuntimeError("NeoForge server RCON does not match the local sandbox configuration")
        self.connection = socket.create_connection(("127.0.0.1", config["minecraftRconPort"]), timeout=3)
        self.rcon.exchange(self.connection, self.request, 3, config["rconToken"])

    def command(self, command):
        self.request += 1
        reply = self.rcon.exchange(self.connection, self.request, 2, command)
        self.data.setdefault("commands", []).append({"at": time.time(), "command": command, "reply": reply})
        return reply.strip()

    def session(self, gameplay=False):
        if gameplay:
            self.check_escape()
        host, server = fresh(HOST_STATUS), fresh(MC_STATUS)
        if not host.get("dedicated") or host.get("map") != "cs_assault" or server.get("map") != "cs_assault":
            raise RuntimeError("This exercise requires the dedicated cs_assault sandbox")
        if not host.get("world") or host["world"] == "0" or host["world"] != server.get("world"):
            raise RuntimeError("The two authoritative servers disagree on the map session")
        if self.data.get("world") and host["world"] != self.data["world"]:
            raise RuntimeError("Map session changed; old fixture coordinates must not be modified")
        for role in ("host", "minecraft"):
            if self.data.get("processes", {}).get(role) != process_identity(ROLES[role]):
                raise RuntimeError("A server restarted; fixture ownership is no longer valid")
        if not gameplay:
            return server
        for role in ("client", "minecraftClient"):
            if self.data["processes"][role] != process_identity(ROLES[role]):
                raise RuntimeError("A paired client restarted; comparison stopped")
        sample_deadline = time.monotonic() + .2
        sample = self.monitor.snapshot()
        while sample is None and time.monotonic() < sample_deadline:
            time.sleep(.01)
            sample = self.monitor.snapshot()
        n = sample["native"] if sample else {}
        raw = fresh(LOGS / "goldcraft-client-status.json", 1500)
        jraw = fresh(LOGS / "minecraft-client-status.json", 2500)
        if (not self.monitor.paired_gameplay(sample) or not n.get("windowFocused")
                or not 0 <= sample["statusAgeMs"] <= 1500 or not 0 <= sample["javaStatusAgeMs"] <= 2500
                or not n.get("minecraftControl") or not n.get("minecraftForm")
                or raw.get("inputButtons") != 0 or not jraw.get("inputActive")
                or n.get("world") != host["world"] or raw.get("world") != host["world"]
                or jraw.get("world") != host["world"]):
            raise RuntimeError("B must be fresh, focused, actively paired in MC form and without input/menu")
        matches = [p for p in server.get("players", []) if p.get("slot") == raw.get("playerSlot")
                   and p.get("serial") == raw.get("playerSerial") == jraw.get("serial")
                   and p.get("life") == raw.get("playerLife") == n.get("playerLife") == jraw.get("life")]
        if len(matches) != 1:
            raise RuntimeError("No unique authoritative server player matches B's current lifecycle")
        player = matches[0]
        if (not all(player.get(key) for key in ("ready", "enabled", "alive", "minecraftForm",
                                               "handlerMatchesPlayer", "trackedEntityMatchesPlayer"))
                or any(player.get(key) for key in ("teleportPending", "removed", "deathAwaitingAuthority"))
                or not re.fullmatch(r"goldcraft:cs_assault_[0-9a-f]{8}", player.get("dimension", ""))):
            raise RuntimeError("The authoritative Minecraft player is not ready for a fixture")
        identity = {key: player[key] for key in ("uuid", "slot", "serial", "life", "dimension")}
        if self.data.get("player") and identity != self.data["player"]:
            raise RuntimeError("Player identity, life or dimension changed; comparison stopped")
        if any(len(v) != 3 or not all(math.isfinite(x) for x in v)
               for v in (player["position"], n["origin"], n["viewAngles"])):
            raise RuntimeError("Invalid pose diagnostics")
        if not -60 <= player["position"][1] <= 312 or max(abs(player["position"][i]) for i in (0, 2)) > 10000:
            raise RuntimeError("Player pose is outside the bounded GoldSrc fixture area")
        if self.anchor and max(self.monitor.delta(n, self.anchor)) >= .1:
            raise RuntimeError("Observer moved from the original fixture view")
        return sample, player

    def stable(self, seconds=3, expected_lights=None):
        deadline = time.monotonic() + self.args.wait_stable
        anchor = None
        started = 0
        stable_lights = None
        stable_player = None
        while time.monotonic() < deadline:
            try:
                sample, player = self.session(True)
                lights = light_snapshot(sample)
            except InterruptedError:
                raise
            except LightSnapshotPending:
                # Uploads and the optional screenshot may briefly delay a
                # light update. Re-establish three fresh seconds before the
                # next capture; session() still rejects pose/life changes.
                anchor = None
                time.sleep(.25)
                continue
            except (OSError, ValueError, RuntimeError):
                if self.anchor:
                    raise
                anchor = None
                time.sleep(.25)
                continue
            if expected_lights is not None and lights != expected_lights:
                raise RuntimeError("Light snapshot changed outside the placement phase")
            identity = tuple(player[key] for key in ("uuid", "slot", "serial", "life", "dimension"))
            if (anchor is None or max(self.monitor.delta(sample["native"], anchor)) >= .1
                    or sample["native"]["world"] != anchor["world"]
                    or identity != stable_player or lights != stable_lights):
                anchor, started, stable_lights = sample["native"], time.monotonic(), lights
                stable_player = identity
            elif time.monotonic() - started >= seconds:
                return sample, player, lights
            time.sleep(.25)
        raise TimeoutError("No three-second foreground stationary paired view with stable light counts")

    def block_test(self, position, block, margin=False):
        cells = [position]
        if margin:
            cells += [[position[i] + (direction if i == axis else 0) for i in range(3)]
                      for axis in range(3) for direction in (-1, 1)]
        return " ".join(f"if block {' '.join(map(str, cell))} minecraft:{block}" for cell in cells)

    def add_lights(self, target):
        for cell in self.data["candidates"]:
            if sum(item["state"] == "placed" for item in self.data["blocks"]) >= target:
                return
            if any(item["position"] == cell for item in self.data["blocks"]):
                continue
            self.session(True)
            prefix = f"execute in {self.data['player']['dimension']} "
            predicate = self.block_test(cell, "air", margin=True)
            if self.command(prefix + predicate) != "Test passed":
                continue
            # Intent is durable BEFORE any block mutation. A lost reply cannot
            # grant ownership merely because a glowstone is found afterwards.
            item = {"position": cell, "original": "minecraft:air", "state": "placing"}
            self.data["blocks"].append(item)
            self.save()
            self.session(True)
            reply = self.command(prefix + predicate + " run setblock " + " ".join(map(str, cell))
                                 + " minecraft:glowstone keep")
            if reply == "Changed the block at " + ", ".join(map(str, cell)):
                item["state"] = "placed"
            elif reply == "Test failed" or reply == "Could not set the block":
                item["state"] = "not-placed"
            else:
                item["state"] = "uncertain"
                self.save()
                raise RuntimeError("Unrecognized placement reply; inspect the durable recovery manifest")
            self.save()
        raise RuntimeError(f"Not enough untouched air cells for {target} lights; fixture not expanded")

    def verify_blocks(self):
        self.session(True)
        for item in self.data["blocks"]:
            if item["state"] == "placed":
                command = f"execute in {self.data['player']['dimension']} " + self.block_test(item["position"], "glowstone")
                if self.command(command) != "Test passed":
                    raise RuntimeError("A fixture block was changed externally; comparison stopped")
        self.save()
        self.session(True)

    def capture(self, label):
        if not self.args.capture:
            return None
        source = LOGS / "goldcraft-frame.bmp"
        previous = source.stat().st_mtime_ns if source.exists() else 0
        self.session(True)
        self.renderer.command("capture")
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            self.session(True)
            if source.exists() and source.stat().st_mtime_ns > previous:
                time.sleep(.1)
                target = OUTPUT / f"{label}.bmp"
                local_path(target, OUTPUT)
                shutil.copy2(source, target)
                sample, _ = self.session(True)
                return {"path": str(target.relative_to(ROOT)),
                        "staticShadowsEnabled": sample["native"].get("sceneStaticShadowsEnabled"),
                        "note": "Scene capture outside timing; not an OFF/ON pixel comparison."}
            time.sleep(.1)
        raise TimeoutError("The diagnostic framebuffer capture did not finish")

    def measure(self, label, lights, cache=None, reference=None, face_cull=None, benchmark=True,
                depth_executor=None, bone_palette=None):
        self.verify_blocks()
        before, _, _ = self.stable(expected_lights=lights)
        command = [sys.executable, str(ROOT / "tools/Monitor-LivePerformance.py"),
                   "--seconds", str(self.args.seconds), "--wait-stable", "10", "--label", label]
        if cache is not None:
            command += ["--scene-static-shadows", str(cache)]
            if benchmark:
                command += ["--benchmark-fps", "300"]
        studio_requests = {"FaceCull": face_cull, "DepthExecutor": depth_executor, "BonePaletteCache": bone_palette}
        for option, value in (("studio-shadow-face-cull", face_cull),
                              ("studio-shadow-depth-executor", depth_executor),
                              ("studio-bone-palette-cache", bone_palette)):
            if value is not None:
                command += ["--" + option, str(value)]
        if reference:
            command += ["--reference", str(reference)]
        log_path = OUTPUT / f"{label}.log"
        started = time.time_ns()
        guard_error = None
        with log_path.open("x", encoding="utf-8") as log:
            # Persist launch intent first. A hard crash before identity is saved
            # must fail closed on recovery; it must not invent a PID to kill.
            self.data["monitor"] = {"active": True, "phase": "launching", "label": label}
            self.save()
            process = None
            # Do not kill Monitor when a gameplay gate fails: its finally block
            # restores cache/cap/Studio options. Stop this comparison, wait its bounded capture,
            # then clean up; never start the next phase after an invalid sample.
            try:
                process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                identity = live_process(process.pid)
                if identity is None:
                    raise RuntimeError("Monitor exited before its process identity could be recorded")
                self.data["monitor"].update({key: identity[key] for key in ("pid", "birth100ns")})
                self.data["monitor"]["phase"] = "running"
                self.save()
                deadline = time.monotonic() + self.args.seconds + 90
                while process.poll() is None:
                    try:
                        sample, _ = self.session(True)
                        if light_snapshot(sample) != lights:
                            raise RuntimeError("Light counts changed during measurement")
                    except InterruptedError:
                        raise
                    except (OSError, ValueError, RuntimeError) as error:
                        guard_error = guard_error or str(error)
                    if time.monotonic() > deadline:
                        raise TimeoutError("Monitor exceeded its bounded run; outer restoration is required")
                    time.sleep(.25)
            finally:
                if process is not None and process.poll() is None:
                    # An interrupt must never leave a Monitor changing options
                    # concurrently with rollback. Terminate only this owned child;
                    # outer finally restores all settings from the manifest.
                    process.terminate()
                    process.wait(timeout=5)
                if process is None or process.poll() is not None:
                    self.data["monitor"].update(active=False, phase="exited", endedAt=time.time(),
                                                exitCode=process.returncode if process is not None else None)
                    self.save()
        paths = [path for path in OUTPUT.glob(label + "-*.json") if path.stat().st_mtime_ns >= started]
        if len(paths) != 1:
            raise RuntimeError("Monitor did not produce one unambiguous evidence report: " + str(log_path))
        path = paths[0]
        result = read_json(path)
        record = {"report": str(path.relative_to(ROOT)),
                  "cache": cache, "studioFaceCull": face_cull, "studioDepthExecutor": depth_executor,
                  "studioBonePaletteCache": bone_palette,
                  "lightsBefore": light_snapshot(before),
                  "guardError": guard_error, "monitorExitCode": process.returncode,
                  "summary": result.get("summary", {})}
        self.data["measurements"].append(record)
        self.save()
        if process.returncode or guard_error or not result.get("summary", {}).get("eligible"):
            raise RuntimeError("Ineligible Monitor sample; further comparisons stopped")
        summary = result["summary"]
        for feature, requested in studio_requests.items():
            if requested is not None and (
                    summary.get("requestedStudio" + feature) != requested
                    or summary.get("activeStudio" + feature) != requested
                    or summary.get("studio" + feature + "Restored") is not True
                    or not self.monitor.stable_studio_feature(
                        result.get("samples", []), result.get("profiles", {}).get("renderer", {}).get("frames", []),
                        requested, requested, feature)
                    or summary.get("activeStaticShadows") is not True):
                raise RuntimeError(f"Monitor did not verify the requested Studio {feature} mode with cache enabled")
        if not benchmark and summary.get("activeCaps") != NORMAL_CAPS:
            raise RuntimeError("The normal-cap sample did not retain 99.5 FPS/override 0/vsync 0")
        if any(light_snapshot(sample) != lights for sample in result.get("samples", [])):
            raise RuntimeError("Monitor observed an unstable light snapshot")
        self.verify_blocks()
        after, _, _ = self.stable(expected_lights=lights)
        record["lightsAfter"] = light_snapshot(after)
        record["rendererVisibleMinecraftLights"] = None
        frames = [frame for frame in result.get("profiles", {}).get("renderer", {}).get("frames", [])
                  if frame.get("interval", 0) > 0]
        counts = [frame.get("lightsVisible") for frame in frames]
        if not counts or any(type(count) is not int or count < 0 for count in counts):
            raise RuntimeError("Renderer frames do not expose valid lightsVisible telemetry")
        record["rendererAllVisibleLights"] = {"min": min(counts), "max": max(counts),
                                             "mean": sum(counts) / len(counts), "frames": len(counts)}
        record["rendererAllVisibleLightsStable"] = min(counts) == max(counts)
        record["rendererShadowCallsPerFrame"] = {
            key: value for key, value in record["summary"].get("renderer", {}).get("meanCounts", {}).items()
            if key in ("pointShadowCalls", "spotShadowCalls", "directionalShadowCalls")}
        record["countNote"] = "lightsVisible is the actual Renderer visible-light set size, including host lights. Shadow calls include host lights and views. Neither identifies the MC-only visible subset."
        self.save()
        # Ordinary Bot gameplay creates short-lived host lights. Their total is
        # observed workload, not a fixture invariant; MC counts and the exact
        # placed blocks remain guarded throughout the fixed-view comparison.
        return path, result

    def wait_monitor_exit(self, timeout=30):
        monitor = self.data.get("monitor", {})
        if not monitor.get("active"):
            return
        if any(type(monitor.get(key)) is not int or monitor[key] <= 0 for key in ("pid", "birth100ns")):
            raise RuntimeError("Monitor launch identity is incomplete; recovery refused pending manual inspection")
        deadline = time.monotonic() + min(max(timeout, 0), 30)
        while True:
            observed = live_process(monitor["pid"])
            if observed is None or observed["birth100ns"] != monitor["birth100ns"]:
                monitor.update(active=False, phase="observed-exited", endedAt=time.time())
                self.save()
                return
            if time.monotonic() >= deadline:
                raise RuntimeError("The recorded Monitor is still running; no options or blocks were restored. Retry --cleanup after it exits")
            time.sleep(.25)

    def restore_options(self):
        if not self.data.get("original"):
            return
        if process_identity(ROLES["client"]) != self.data["processes"]["client"]:
            raise RuntimeError("B restarted; options of a new client will not be modified")
        errors = []
        actions = ["benchmark_framerate 0", f"scene_static_shadows {int(self.data['original']['staticShadows'])}"]
        studio_original = {feature: self.data["original"]["studio" + feature]
                           for feature in self.monitor.STUDIO_COMMANDS if "studio" + feature in self.data["original"]}
        for feature, value in studio_original.items():
            if type(value) is not int or value not in (0, 1):
                raise ValueError(f"Invalid original Studio {feature} value in recovery manifest")
            actions.append(f"{self.monitor.STUDIO_COMMANDS[feature]} {value}")
        for action in actions:
            try:
                self.renderer.command(action)
            except Exception as error:
                errors.append(f"{action}: {error}")
        okay = False
        try:
            state = fresh(LOGS / "goldcraft-client-status.json", 1500)
            okay = ({key: state.get(key) for key in NORMAL_CAPS} == self.data["original"]["caps"]
                    and not state.get("benchmarkFramerate")
                    and state.get("sceneStaticShadowsEnabled") == self.data["original"]["staticShadows"])
            for feature, value in studio_original.items():
                okay = okay and self.monitor.studio_feature(state, feature) == value
            if not okay:
                errors.append("Renderer option/cap restoration failed final verification")
        except Exception as error:
            okay = False
            errors.append(f"Final restoration verification: {error}")
        self.data["optionsRestored"] = okay
        self.data["restoreErrors"] = errors
        if errors:
            raise RuntimeError("; ".join(errors))

    def cleanup(self):
        self.data["cleanupErrors"] = []
        if any(item["state"] in ("placed", "removing") for item in self.data["blocks"]):
            # Discard a possibly partial RCON response after a timeout, then
            # authenticate a fresh connection only to the same running servers.
            self.session()
            self.connection.close()
            self.connection = None
            self.connect()
        for item in self.data["blocks"]:
            if item["state"] not in ("placed", "removing"):
                continue
            try:
                # No foreground/life requirement for rollback, but both server
                # processes and the authoritative map epoch must still match.
                self.session()
                prefix = f"execute in {self.data['player']['dimension']} "
                position = " ".join(map(str, item["position"]))
                present = self.command(prefix + self.block_test(item["position"], "glowstone"))
                if present == "Test failed":
                    item["state"] = "left-unchanged"
                elif present == "Test passed":
                    item["state"] = "removing"
                    self.save()
                    self.session()
                    reply = self.command(prefix + self.block_test(item["position"], "glowstone")
                                         + f" run setblock {position} minecraft:air")
                    if reply != "Changed the block at " + ", ".join(map(str, item["position"])):
                        raise RuntimeError("Cleanup was not acknowledged")
                    item["state"] = "removed"
                else:
                    raise RuntimeError("Cleanup block observation was unavailable; ownership remains pending")
                self.save()
            except Exception as error:
                self.data["cleanupErrors"].append(str(error))
                # A transport/session failure must not be retried blindly.
                break
        self.data["cleanupComplete"] = not any(item["state"] in PENDING for item in self.data["blocks"])
        self.save()

    def run(self):
        self.arm_escape()
        self.check_escape()
        self.data["processes"] = {role: process_identity(path) for role, path in ROLES.items()}
        self.save()
        first, player, baseline = self.stable()
        native = first["native"]
        caps = {key: native.get(key) for key in NORMAL_CAPS}
        if caps != NORMAL_CAPS or native.get("benchmarkFramerate"):
            raise RuntimeError("Start from normal 99.5 FPS/override 0/vsync 0; this script does not set gameplay defaults")
        if not native.get("sceneStaticShadowsApi") or type(native.get("sceneStaticShadowsEnabled")) is not bool:
            raise RuntimeError("Live static-shadow API is required for OFF/ON measurements")
        original = {"caps": caps, "staticShadows": native["sceneStaticShadowsEnabled"]}
        depth_modes = getattr(self.args, "depth_executor_modes", None)
        palette_modes = getattr(self.args, "bone_palette_modes", None)
        feature = ("FaceCull" if self.args.face_cull_modes is not None else
                   "DepthExecutor" if depth_modes is not None else
                   "BonePaletteCache" if palette_modes is not None else None)
        if feature:
            original["studio" + feature] = self.monitor.studio_feature(native, feature)
        modes = (self.args.face_cull_modes if feature == "FaceCull" else depth_modes if feature == "DepthExecutor"
                 else palette_modes if feature == "BonePaletteCache" else self.args.cache_modes)
        feature_parameter = {"FaceCull": "face_cull", "DepthExecutor": "depth_executor",
                             "BonePaletteCache": "bone_palette"}.get(feature)
        self.anchor = native
        self.data.update(world=native["world"], player={k: player[k] for k in ("uuid", "slot", "serial", "life", "dimension")},
                         anchor={k: native[k] for k in ("origin", "viewAngles")}, position=player["position"],
                         original=original, cacheModes=self.args.cache_modes, faceCullModes=self.args.face_cull_modes,
                         depthExecutorModes=depth_modes, bonePaletteModes=palette_modes,
                         baselineLights=baseline, candidates=candidates(player["position"], native["viewAngles"][1]))
        self.save()
        self.connect()
        # Authoritative diagnostics supply UUID/dimension/position. Cross-check
        # the actual RCON target before issuing the first block command.
        dimension = self.command(f"data get entity {player['uuid']} Dimension")
        if not dimension.endswith('"' + player["dimension"] + '"'):
            raise RuntimeError("RCON player dimension differs from the paired server diagnostics")
        position_reply = self.command(f"data get entity {player['uuid']} Pos")
        match = re.search(r"\[([^\]]+)\]", position_reply)
        position = [float(value.strip().removesuffix("d")) for value in match.group(1).split(",")] if match else []
        if len(position) != 3 or any(not math.isfinite(value) for value in position) or any(
                abs(a - b) >= .01 for a, b in zip(position, player["position"])):
            raise RuntimeError("RCON player position differs from the paired server diagnostics")
        previous = baseline
        for count in self.args.counts:
            self.add_lights(count)
            _, _, lights = self.stable()
            if any(lights[key] <= previous[key] for key in LIGHT_KEYS):
                raise RuntimeError("Placed fixture lights did not increase both observed light counts")
            stage = {"placedBlocks": count, "exportedLights": lights["lights"],
                     "submittedLights": lights["sceneLightsSubmitted"],
                     "exportedDeltaFromBaseline": lights["lights"] - baseline["lights"],
                     "rendererVisibleMinecraftLights": None,
                     "screenshot": self.capture(f"{self.data['run']}-{count}-scene")}
            self.data["stages"].append(stage)
            self.save()
            captures = {}
            reference = None
            visible_counts = {}
            for mode in modes:
                name = {"FaceCull": "face-", "DepthExecutor": "depth-", "BonePaletteCache": "palette-"}.get(feature, "") + ("on" if mode else "off")
                path, result = self.measure(f"{self.data['run']}-{count}-{name}", lights,
                                            cache=1 if feature else mode, reference=reference,
                                            **({feature_parameter: mode} if feature else {}))
                reference = path
                captures[mode] = result
                stage[name] = str(path.relative_to(ROOT))
                visible_counts[name] = self.data["measurements"][-1]["rendererAllVisibleLights"]
                self.save()
            # measure() has already required eligibility, equal view/caps,
            # identity and the owned fixture; do not infer a gain from one mode.
            comparable = set(captures) == {0, 1} and all(result["summary"].get("eligible") is True
                                                        for result in captures.values())
            if comparable:
                comparable = sum(result["summary"].get("referenceMatches") is True
                                 for result in captures.values()) == 1
            if feature and comparable:
                comparable = all(result["summary"].get("activeStudio" + feature) == mode
                                 and result["summary"].get("activeStaticShadows") is True
                                 for mode, result in captures.items())
            stage.update(comparable=comparable, comparisonKind="studioShadow" + feature if feature else "staticShadowCache",
                         rendererAllVisibleLights=visible_counts,
                         workloadNote="Same observer and MC fixture; ordinary Bot and host dynamic-light activity can differ between intervals.")
            if comparable:
                gain_key = "studio" + feature + "FpsGainPercent" if feature else "fpsGainPercent"
                stage[gain_key] = 100 * (captures[1]["summary"]["renderer"]["observedFps"] /
                                       captures[0]["summary"]["renderer"]["observedFps"] - 1)
            previous = lights
            self.save()
        if feature:
            self.measure(f"{self.data['run']}-normal", previous, cache=1, benchmark=False, **{feature_parameter: 1})
        else:
            self.measure(f"{self.data['run']}-normal", previous)
        self.data["outcome"] = ("eligible comparisons" if all(stage["comparable"] for stage in self.data["stages"])
                                else "eligible single-mode samples") + "; screenshots still require visual assessment"


def validate_recovery(data):
    if data.get("schema") != 1 or not re.fullmatch(r"light-perf-[0-9]+", data.get("run", "")):
        raise ValueError("Not a light-performance recovery manifest")
    player = data.get("player", {})
    if not re.fullmatch(r"goldcraft:cs_assault_[0-9a-f]{8}", player.get("dimension", "")):
        raise ValueError("Recovery dimension is not a managed cs_assault dimension")
    original = data.get("original", {})
    for key, modes in (("studioFaceCull", "faceCullModes"), ("studioDepthExecutor", "depthExecutorModes"),
                       ("studioBonePaletteCache", "bonePaletteModes")):
        if key in original and (type(original[key]) is not int or original[key] not in (0, 1)):
            raise ValueError(f"Invalid original {key} value in recovery manifest")
        if data.get(modes) is not None and key not in original:
            raise ValueError(f"{key} recovery requires the original cvar value")
    allowed = candidates(data["position"], data["anchor"]["viewAngles"][1])
    if len(data["blocks"]) > 64:
        raise ValueError("Recovery manifest exceeds the bounded fixture")
    seen = set()
    for item in data["blocks"]:
        cell = item.get("position")
        if (cell not in allowed or any(type(value) is not int for value in cell)
                or tuple(cell) in seen or item.get("original") != "minecraft:air"):
            raise ValueError("Invalid recovery block coordinate or ownership record")
        seen.add(tuple(cell))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--counts", type=int, nargs="+", choices=(8, 32, 64), default=[8, 32])
    parser.add_argument("--cache-modes", type=int, nargs="+", choices=(0, 1), default=[0, 1],
                        help="Cache modes to sample; a single mode gives no improvement comparison")
    studio_modes = parser.add_mutually_exclusive_group()
    studio_modes.add_argument("--face-cull-modes", type=int, nargs="+", choices=(0, 1),
                        help="Compare Studio shadow face culling; requires --cache-modes 1, and a single mode gives no gain")
    studio_modes.add_argument("--depth-executor-modes", type=int, nargs="+", choices=(0, 1),
                        help="Compare Studio shadow depth executor; requires --cache-modes 1, and a single mode gives no gain")
    studio_modes.add_argument("--bone-palette-modes", type=int, nargs="+", choices=(0, 1),
                        help="Compare exact bone-palette reuse; requires --cache-modes 1")
    parser.add_argument("--seconds", type=int, choices=range(5, 26), default=8)
    parser.add_argument("--wait-stable", type=int, choices=range(4, 121), default=15)
    parser.add_argument("--capture", action="store_true", help="Capture one native scene BMP per light-count stage outside timing")
    parser.add_argument("--cleanup", type=Path, help="Recover only acknowledged fixture blocks from this manifest")
    args = parser.parse_args()
    if args.counts != sorted(set(args.counts)):
        parser.error("counts must be unique and increasing")
    if len(args.cache_modes) != len(set(args.cache_modes)):
        parser.error("cache modes must be unique")
    for name, modes in (("face-cull", args.face_cull_modes), ("depth-executor", args.depth_executor_modes),
                        ("bone-palette", args.bone_palette_modes)):
        if modes is not None:
            if len(modes) != len(set(modes)):
                parser.error(name + " modes must be unique")
            if args.cache_modes != [1]:
                parser.error(name + " modes require --cache-modes 1")
    if sys.platform != "win32":
        parser.error("This exercise verifies the local Windows sandbox processes")
    local_path(OUTPUT, ROOT / "analysis")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    import msvcrt
    lock_path = local_path(OUTPUT / "light-performance.lock", OUTPUT)
    with lock_path.open("a+b") as lock:
        if lock.tell() == 0:
            lock.write(b"0")
            lock.flush()
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        if args.cleanup:
            path = local_path(args.cleanup, OUTPUT)
            data = read_json(path)
            validate_recovery(data)
        else:
            for old_path in OUTPUT.glob("light-perf-*-manifest.json"):
                old = read_json(old_path)
                if (old.get("monitor", {}).get("active")
                        or (old.get("original") and old.get("optionsRestored") is not True)
                        or any(item.get("state") in PENDING for item in old.get("blocks", []))):
                    raise RuntimeError("Resolve the previous recovery manifest first: " + str(old_path))
            run = f"light-perf-{time.time_ns()}"
            path = OUTPUT / f"{run}-manifest.json"
            data = {"schema": 1, "run": run, "outcome": "not started", "blocks": [],
                    "stages": [], "measurements": [], "commands": [],
                    "limits": "MC air does not prove BSP clearance. Renderer lightsVisible includes host lights; the MC-only visible subset and full snapshot hash are unavailable. Count stability is not content identity."}
        exercise = Exercise(args, path, data)
        failed = False
        try:
            if args.cleanup:
                exercise.session()
                exercise.connect()
            else:
                exercise.run()
        except BaseException as error:
            failed = True
            data["outcome"] = "stopped; comparison is incomplete"
            data["error"] = f"{type(error).__name__}: {error}"
        finally:
            rollback_allowed = False
            try:
                # Never compete with a Monitor left behind by a killed parent.
                # Recovery only observes its recorded PID+birth; it never kills
                # a process selected from an arbitrary manifest.
                exercise.wait_monitor_exit()
                rollback_allowed = True
            except Exception as error:
                data["recoveryBlocked"] = str(error)
                failed = True
            try:
                if rollback_allowed:
                    try:
                        exercise.restore_options()
                    except Exception as error:
                        data["restoreError"] = str(error)
                        failed = True
                if rollback_allowed and exercise.connection:
                    exercise.cleanup()
                else:
                    data["cleanupComplete"] = rollback_allowed and not any(item["state"] in PENDING for item in data["blocks"])
            except Exception as error:
                data.setdefault("cleanupErrors", []).append(str(error))
                data["cleanupComplete"] = False
                failed = True
            finally:
                if exercise.connection:
                    exercise.connection.close()
                exercise.save()
        print(json.dumps({"manifest": str(path), "outcome": data["outcome"],
                          "cleanupComplete": data.get("cleanupComplete"), "optionsRestored": data.get("optionsRestored"),
                          "error": data.get("error"), "restoreError": data.get("restoreError"),
                          "recoveryBlocked": data.get("recoveryBlocked")}), flush=True)
        return 1 if failed or not data.get("cleanupComplete") else 0


if __name__ == "__main__":
    sys.exit(main())
