"""Exercise sampled world-cell commits on real cs_assault and the native bridge.

The authenticated authority peer and fake player's input are controlled fixtures.
Successful tunnel cuts use the production mining request, never arbitrary-box
fixture commands. One explicit concurrent-edit probe uses the fixture command
to invalidate a saved ticket. This does not validate B input, rendering or FPS.
"""
import importlib.util
import json
import math
from pathlib import Path
import struct
import time

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("world_fixture", ROOT / "tools/Exercise-WorldCarving.py")
world = importlib.util.module_from_spec(spec)
spec.loader.exec_module(world)


class Run(world.Run):
    def aim_world(self, x=80, z=400):
        self.eye, end = [x, 2784, z], [x, 2976, z]
        hit = self.trace(self.eye, end)
        self.target_slot = self.target_serial = self.model = 0
        self.point = hit["end"]
        self.pose(self.eye, self.point)
        return hit

    def snapshot(self):
        revision = self.state()["mapEditRevision"]
        self.peer.send(61, struct.pack("<Q", self.peer.world))
        packet = self.until(lambda: self.peer.take(59, lambda p: struct.unpack_from("<Q", p, 8)[0] >= revision), "Committed geometry snapshot")
        epoch, delivered, count = struct.unpack_from("<QQI", packet)
        assert epoch == self.peer.world and len(packet) == 20 + count * 44
        return delivered, [struct.unpack_from("<QIII6f", packet, 20 + i * 44) for i in range(count)]

    def sample(self):
        self.rested()
        sample = self.request(sample=True)
        assert sample["status"] == 0 and sample["kind"] == 2 and sample["edit_revision"] > 0, sample
        return sample

    def end(self):
        self.command("gc_mining_round end")
        self.until(lambda: "terminating=1" in self.command("gc_mining_state"), "Native round end")

    def run(self):
        self.report["scope"] = __doc__
        self.start()
        self.set_mode(2)
        before = self.aim_world()
        self.check("known assault wall is hit by actual engine point trace", before["hit"] == 0 and 0 < before["fraction"] < 1)
        self.rested()
        self.check("world geometry refuses a request without a sample ticket", self.request(sample_event=0)["status"] == 6)
        selected = self.sample()
        expected = [math.floor((v - n / 16) / 32) for v, n in zip(before["end"], before["normal"])]
        self.check("native sample selects solid grid cell across trace backoff", selected["cell"] == expected)
        self.rested()
        self.check("unknown sample ticket cannot commit geometry", self.request(sample_event=selected["event"] + 1000)["status"] == 6)
        self.aim_world(x=112)
        self.rested()
        self.check("a different cell cannot inherit sampled progress", self.request(sample_event=selected["event"])["status"] == 6 and self.state()["mapEditCount"] == 0)
        self.aim_world()
        selected = self.sample()
        time.sleep(2.05)
        self.check("expired sample cannot remove a cell", self.request(sample_event=selected["event"])["status"] == 6)
        selected = self.sample()
        neighbor = [(v + (1 if axis == 0 else 0)) * 32 for axis, v in enumerate(selected["cell"])]
        self.cut((neighbor, [v + 32 for v in neighbor]))
        self.rested()
        self.check("concurrent map revision invalidates saved sample", self.request(sample_event=selected["event"])["status"] == 6)
        self.command("mc_map_mining_persist 0")
        self.restart()
        self.aim_world()
        selected = self.sample()
        started = time.monotonic()
        applied = self.request()
        self.report["firstCommitSeconds"] = time.monotonic() - started
        self.until(lambda: self.state()["mapEditCount"] == 1, "First production cell commit")
        self.check("sampled request commits real geometry", applied["status"] == 0 and applied["before"] == 1 and applied["after"] == 0)
        revision, cuts = self.snapshot()
        cell = selected["cell"]
        self.check("authoritative delivery contains exact sampled box", len(cuts) == 1 and cuts[0][1:4] == (0, 0, 0)
                   and cuts[0][4:7] == tuple(v * 32 for v in cell) and cuts[0][7:10] == tuple((v + 1) * 32 for v in cell))
        after = self.trace([80, 2784, 400], [80, 2976, 400])
        self.check("successful result follows actual native collision replacement", after["fraction"] > before["fraction"] and self.physics()["revision"] == revision)
        self.check("same completed event cannot commit twice", self.request(event=applied["event"])["status"] == 4)
        self.rested()
        self.check("consumed sample cannot commit with a new event", self.request(sample_event=selected["event"])["status"] != 0
                   and self.state()["mapEditRevision"] == revision)
        # All tunnel cells below are produced through native sampling and the
        # authenticated production request. This covers new cavity faces too.
        for z in (368, 400, 432):
            for x in (80, 112):
                for depth in range(8):
                    hit = self.aim_world(x, z)
                    if hit["fraction"] == 1:
                        break
                    assert hit["hit"] == 0, hit
                    sample = self.sample()
                    result = self.request()
                    assert result["status"] == 0, (sample, result)
                else:
                    raise AssertionError("Tunnel did not open within bounded cells")
        opened = self.trace([96, 2784, 400], [96, 2976, 400], 1)
        self.check("sampled adjacent cells open the real standing hull", opened["fraction"] == 1 and not opened["start"])
        rim = self.trace([120, 2784, 400], [120, 2976, 400], 1)
        self.check("standing hull still stops at tunnel rim", rim["fraction"] < 1)
        self.command("mc_map_mining_persist 1")
        count, revision = self.state()["mapEditCount"], self.state()["mapEditRevision"]
        self.end(); self.restart()
        self.check("production cuts persist across configured round restart", self.state()["mapEditCount"] == count
                   and self.state()["mapEditRevision"] == revision and self.trace([96, 2784, 400], [96, 2976, 400], 1)["fraction"] == 1)
        self.command("mc_map_mining_persist 0")
        self.end()
        self.until(lambda: self.state()["mapEditCount"] == 0, "Round restores production cells")
        restored = self.trace([80, 2784, 400], [80, 2976, 400])
        self.check("round end restores original point collision and advances revision", abs(restored["fraction"] - before["fraction"]) < 1e-7
                   and self.state()["mapEditRevision"] > revision)
        stats = self.physics()
        self.check("native geometry producer has no physics error fallback", stats["failures"] == 0 and not stats["error"])
        self.report["productionTunnelCells"] = count
        self.report["passed"] = True


if __name__ == "__main__":
    run = Run()
    try:
        run.run()
    except Exception as error:
        run.report.update(error=str(error), passed=False)
        raise
    finally:
        run.close(output=ROOT / f"analysis/world-carving/producer-native-{time.time_ns()}.json")
