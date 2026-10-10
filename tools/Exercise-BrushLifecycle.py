"""Real ReHLDS/GameDLL brush cleanup and optional local-sampling API checks.

Uses the independent non-LAN cs_assault fixture and an authenticated test peer.
Cloned native func_wall entities use actual *11/*12 models and native lifecycle;
arbitrary cuts and Replace failures are fixture-only. No B/JVM/main deployment,
dynamic production mining, graphics, networked client or FPS acceptance.
"""
import importlib.util
import argparse
import json
import math
from pathlib import Path
import re
import time

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("producer", ROOT / "tools/Exercise-MiningProducer.py")
producer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(producer)


def transform(point, origin, angles):
    # Independent GoldSrc Euler basis, inverse of world-to-model dot products.
    p, y, r = map(math.radians, angles)
    sp, cp, sy, cy, sr, cr = math.sin(p), math.cos(p), math.sin(y), math.cos(y), math.sin(r), math.cos(r)
    forward = [cp * cy, cp * sy, -sp]
    right = [-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp]
    up = [cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp]
    return [origin[i] + forward[i] * point[0] - right[i] * point[1] + up[i] * point[2] for i in range(3)]


def values(items):
    return " ".join(f"{v:.6f}" for v in items)


class Run(producer.Run):
    def prepare(self):
        super().prepare()
        if getattr(self, "production_smoke", False):
            game = producer.world.rounds.mining.base.GAME
            self.copy(ROOT / "build/regamedll/Release/mp.dll", game / "cstrike/dlls/mp.dll")

    def smoke(self):
        self.production_smoke = True
        self.report["scope"] = "Real production engine/GameDLL command registry, fixture environment deliberately set; no player or GUI."
        self.start_native()
        def ready():
            try:
                return "ReHLDS" in self.command("version")
            except OSError:
                return False
        self.until(ready, "Production DLL startup", seconds=45)
        self.check("production dedicated server keeps non-LAN policy", '"sv_lan" is "0"' in self.command("sv_lan"))
        self.check("production mining cvar registered", '"mc_map_mining"' in self.command("mc_map_mining"))
        for name in ("gc_brush_fixture", "gc_carve_fixture", "gc_carve_trace", "gc_carve_move", "gc_edits_fixture"):
            answer = self.command("cmdlist " + name)
            self.check("production GameDLL has no " + name, bool(re.search(r"\b0 Commands for \[" + name + r"\]", answer)))
        self.report["passed"] = True

    def create(self, origin=(0, 0, 0), angles=(0, 0, 0), model=11):
        answer = self.command(f"gc_brush_fixture create {model} {values((*origin, *angles))}")
        match = re.search(r"slot=(\d+) serial=(\d+) model=\*(\d+)", answer)
        if not match:
            raise RuntimeError("Missing native clone: " + answer)
        return dict(zip(("slot", "serial", "model"), map(int, match.groups())))

    def sample_brush(self, brush, start, end):
        answer = self.command(f"gc_brush_fixture sample {brush['slot']} {values((*start, *end))}")
        match = re.search(r"sampled=(\d+) cell=(-?\d+),(-?\d+),(-?\d+)", answer)
        if not match:
            raise RuntimeError("Missing loaded engine sample result: " + answer)
        return bool(int(match[1])), [int(v) for v in match.groups()[1:]]

    def expected_count(self, count):
        self.until(lambda: self.state()["mapEditCount"] == count, "Committed brush cleanup")
        revision, cuts = self.snapshot()
        if len(cuts) != count:
            raise AssertionError("Authority snapshot count disagrees with committed ledger")
        return revision, cuts

    def run(self):
        self.report["scope"] = __doc__
        self.start()
        api = self.command("gc_brush_fixture api")
        self.check("both engine interfaces load through real DLL factories, dynamic gate stays closed",
                   "physics=1 sample=1 dynamic=0" in api)
        self.set_mode(2)
        self.target(11)
        self.command("gc_mining_edit 100 0")
        self.rested()
        sampled = self.request(sample=True)
        self.check("dynamic gate retains material reads but cannot issue a cut ticket",
                   sampled["status"] == 0 and sampled["kind"] == 2 and sampled["edit_revision"] == 0)
        self.rested()
        self.check("authenticated production dynamic commit remains unavailable", self.request()["status"] == 9)

        a, b = self.create(), self.create((256, 0, 0))
        start, end = [-80, -28, 5], [80, -28, 5]
        original = self.trace(start, end, 0, a["slot"])
        self.check("native *11 clone has original point collision", 0 < original["fraction"] < 1 and original["hit"] == a["slot"])
        ok, expected = self.sample_brush(a, start, end)
        self.check("loaded API samples the solid local cell on actual assault door", ok and expected == [-1, -1, 0])
        poses = [((128, 256, 64), angle) for angle in ((0, 90, 0), (0, 37, 0), (15, 37, 8), (-24, -67, 19))]
        for origin, angles in poses:
            self.command(f"gc_brush_fixture pose {a['slot']} {values((*origin, *angles))}")
            world_start, world_end = transform(start, origin, angles), transform(end, origin, angles)
            hit = self.trace(world_start, world_end, 0, a["slot"])
            ok, cell = self.sample_brush(a, world_start, world_end)
            self.check(f"translated/rotated native local cell {angles}", ok and cell == expected and abs(hit["fraction"] - original["fraction"]) < 1e-5)
        self.command(f"gc_brush_fixture pose {a['slot']} 0 0 0 0 0 0")
        box = ([-8, -52, -44], [8, -4, 44])
        self.cut(box, a["slot"])
        self.expected_count(1)
        self.check("one model instance opens while same-model sibling retains collision",
                   self.trace(start, end, 0, a["slot"])["fraction"] == 1
                   and self.trace([v + (256 if i == 0 else 0) for i, v in enumerate(start)],
                                  [v + (256 if i == 0 else 0) for i, v in enumerate(end)], 0, b["slot"])["fraction"] < 1)
        # A ray outside the hole still finds a surface; the removed ray has none.
        self.check("loaded sample traces edited geometry", self.sample_brush(a, start, end) == (False, [-999, -999, -999])
                   and self.sample_brush(a, [-80, -56, 5], [80, -56, 5])[0])
        self.command(f"gc_brush_fixture solid {a['slot']} 0")
        self.cut(box, b["slot"])
        revision, cuts = self.expected_count(2)
        self.check("temporary SOLID_NOT survives a complete physics replacement", len(cuts) == 2 and self.physics()["targets"] == 2)
        self.check("temporarily inactive brush refuses sample", self.sample_brush(a, [-80, -56, 5], [80, -56, 5]) == (False, [-999, -999, -999]))
        self.command(f"gc_brush_fixture solid {a['slot']} 4")
        self.check("resumed brush keeps its own hole", self.trace(start, end, 0, a["slot"])["fraction"] == 1)
        origin, angles = poses[-1]
        self.command(f"gc_brush_fixture pose {a['slot']} {values((*origin, *angles))}")
        self.check("committed hole follows native translated and rotated brush",
                   self.trace(transform(start, origin, angles), transform(end, origin, angles), 0, a["slot"])["fraction"] == 1)

        self.command("gc_brush_fixture fail 1")
        self.command(f"gc_brush_fixture remove {a['slot']}")
        received = self.peer.received
        time.sleep(.4)
        self.check("failed cleanup rolls back ledger while bridge snapshots keep arriving",
                   self.state()["mapEditCount"] == 2 and self.state()["mapEditRevision"] == revision
                   and self.peer.received > received + 3)
        # Exercise real message processing during persistent failure, not only a
        # local helper continuation. Policy messages and snapshots must progress.
        self.set_mode(1)
        self.check("failed cleanup cannot starve bridge input/policy processing", self.peer.policy[2] == 1)
        self.pose(self.eye, self.point, reverse=True)
        self.check("failed cleanup still applies authenticated pose input", self.peer.actors[self.slot]["flags"] & 32)
        self.pose(self.eye, self.point)
        api = self.command("gc_brush_fixture api")
        self.check("background cleanup retries on multiple native frames", int(re.search(r"failures=(\d+)", api)[1]) > 10)
        self.command("gc_brush_fixture fail 0")
        after, cuts = self.expected_count(1)
        self.check("successful retry removes only stale identity and publishes authority snapshot",
                   after > revision and cuts[0][1:4] == (b["slot"], b["serial"], b["model"]) and self.physics()["targets"] == 1)
        self.command(f"gc_brush_fixture model {b['slot']} 12")
        after_model, _ = self.expected_count(0)
        self.check("actual SET_MODEL replacement retires old cuts", after_model > after and self.physics()["targets"] == 0)

        # Keep a stale engine target and ledger while ED_Free advances the real
        # edict serial and ED_Alloc reuses that exact slot for the same model.
        old = self.create((256, 0, 0))
        self.cut(box, old["slot"])
        self.expected_count(1)
        self.command("gc_brush_fixture fail 1")
        old_slot, old_serial = old["slot"], old["serial"]
        self.command(f"gc_brush_fixture remove {old_slot}")
        time.sleep(.6)
        reused = self.create((256, 0, 0))
        self.check("same native slot with advanced serial cannot inherit stale edited collision",
                   reused["slot"] == old_slot and reused["serial"] != old_serial
                   and self.state()["mapEditCount"] == 1
                   and self.trace([176, -28, 5], [336, -28, 5], 0, reused["slot"])["fraction"] < 1)
        self.report["edictReuse"] = {"oldSlot": old_slot, "newSlot": reused["slot"], "oldSerial": old_serial, "newSerial": reused["serial"]}
        self.command("gc_brush_fixture fail 0")
        self.expected_count(0)
        self.check("background retry removes the reused slot's old incarnation", self.physics()["targets"] == 0)
        self.cut(box, reused["slot"])
        self.command("mc_map_mining_persist 1")
        self.end()
        self.check("persist1 retains dynamic edits at native round end", self.state()["mapEditCount"] == 1)
        self.command("mc_map_mining_persist 0")
        self.restart()
        self.expected_count(0)
        self.check("persist0 restores dynamic geometry on native round cleanup", self.physics()["targets"] == 0)
        self.check("restoration returns original instance collision", self.trace([176, -28, 5], [336, -28, 5], 0, reused["slot"])["fraction"] < 1)
        self.cut(box, reused["slot"])
        self.command("mc_map_mining_persist 1")
        epoch = self.peer.world
        self.peer.close()
        self.peer = None
        self.command("changelevel cs_assault")
        self.reconnect(epoch)
        self.until(lambda: self.state()["mapEditCount"] == 0, "Fresh map has no old brush cuts")
        self.check("actual map reload clears all dynamic edits despite persist1", self.peer.world != epoch and self.physics()["targets"] == 0)
        self.check("map reload rebinds both optional engine interfaces", "physics=1 sample=1 dynamic=0" in self.command("gc_brush_fixture api"))
        self.report["runtimeErrors"] = {p.name: p.read_bytes()[self.error_offsets.get(p, 0):].decode("utf-8", errors="replace")
                                        for p in (producer.world.rounds.mining.base.AMXX / "logs").glob("error_*.log")
                                        if p.stat().st_size > self.error_offsets.get(p, 0)}
        self.check("no AMXX runtime errors", not self.report["runtimeErrors"])
        self.report["passed"] = True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-smoke", action="store_true")
    args = parser.parse_args()
    run = Run()
    try:
        run.smoke() if args.production_smoke else run.run()
    except Exception as error:
        run.report.update(error=str(error), passed=False)
        raise
    finally:
        run.close(output=ROOT / f"analysis/brush-server/runtime-{time.time_ns()}.json")
