"""Real native edit-state transport/lifecycle, with synthetic cuts in a fixture-only DLL.

Does not edit BSP geometry, render a hole, or change player collision. Exercises
the production ledger/round policy and authenticated versioned stream instead.
"""
from pathlib import Path
import importlib.util
import re
import struct
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("edit_rounds", ROOT / "tools/Exercise-MapMiningRounds.py")
rounds = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rounds)


class Run(rounds.Run):
    def __init__(self):
        super().__init__()
        self.report["scope"] = __doc__

    def packet(self, kind):
        return self.until(lambda: self.peer.take(kind), "Map edit packet " + str(kind))

    def snapshot(self):
        body = self.packet(59)
        epoch, revision, count = struct.unpack_from("<QQI", body)
        if epoch != self.peer.world or len(body) != 20 + 44 * count:
            raise ValueError("Map edit snapshot layout")
        cuts = [struct.unpack_from("<QIII6f", body, 20 + 44 * index) for index in range(count)]
        self.report.setdefault("snapshots", []).append(dict(epoch=epoch, revision=revision, count=count))
        return revision, cuts

    def run(self):
        self.start()
        revision, cuts = self.snapshot()
        self.check("new map has an empty versioned snapshot", revision == 1 and not cuts)
        answer = self.command("serverinfo")
        self.check("server advertises matching protocol for native-only clients", bool(re.search(r"mc_protocol\s+" + str(rounds.mining.VERSION), answer)))
        self.command("gc_edits_fixture 2")
        first, second = self.packet(60), self.packet(60)
        a, b = struct.unpack("<QQQIIII6f", first), struct.unpack("<QQQIIII6f", second)
        self.check("two edits retain consecutive distinct deltas", a[:4] == (self.peer.world, 1, 2, 1)
                   and b[:4] == (self.peer.world, 2, 3, 1) and a[4:7] == (0, 0, 0) and a[7:] == (0, 0, 0, 1, 1, 1)
                   and b[7:] == (2, 0, 0, 3, 1, 1))
        self.check("matched engine advertises geometry capability independently of synthetic records", self.peer.policy[3] == 3)
        self.peer.send(60, second)
        self.until(lambda: self.state()["mapEditRevision"] == 3, "Native edit status")
        time.sleep(.2)
        self.check("reverse authority delta cannot mutate native ledger", self.state()["mapEditRevision"] == 3)
        self.command("mc_map_mining_persist 1")
        self.end();self.restart()
        self.check("round end and native cleanup preserve edit revision", self.state()["mapEditCount"] == 2 and self.state()["mapEditRevision"] == 3)
        self.command("mc_map_mining_persist 0")
        self.end()
        cleared = struct.unpack("<QQQI", self.packet(60))
        self.check("restore publishes one clear after real round end", cleared == (self.peer.world, 3, 4, 3))
        self.restart()
        self.command("gc_edits_fixture 300")
        revision, cuts = self.snapshot()
        self.check("journal overflow falls back to complete current state", revision == 304 and len(cuts) == 300
                   and [cut[0] for cut in cuts] == list(range(5, 305)))
        self.peer.send(61, struct.pack("<Q", self.peer.world))
        requested_revision, requested_cuts = self.snapshot()
        self.check("read-only recovery request returns exact full state", requested_revision == revision and requested_cuts == cuts)
        self.command("mc_map_mining_persist 1")
        self.peer.close();self.peer = None;self.reconnect()
        reconnected_revision, reconnected_cuts = self.snapshot()
        self.check("authority reconnect retains edits without replaying mutation", reconnected_revision == revision and reconnected_cuts == cuts)
        epoch = self.peer.world
        self.peer.close();self.peer = None
        self.command("changelevel cs_assault");self.reconnect(previous_epoch=epoch)
        new_revision, new_cuts = self.snapshot()
        self.check("changelevel clears edits despite persistence1", new_revision == 1 and not new_cuts and self.peer.world != epoch)
        self.report["geometryExcavationImplemented"] = False
        self.report["passed"] = True


if __name__ == "__main__":
    run = Run()
    try:
        run.run()
    except Exception as error:
        run.report.update(error=str(error), passed=False)
        raise
    finally:
        run.close(ROOT / f"analysis/world-carving/edit-state-native-{time.time_ns()}.json")
    sys.exit(0 if run.report["passed"] else 1)
