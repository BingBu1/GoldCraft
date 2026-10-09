"""Verify actual native mining persistence, round cancellation and map lifecycle.

Uses the existing independent non-LAN ReHLDS fixture and authenticated authority
test peer. It is not graphical excavation or real Minecraft input acceptance.
"""
from pathlib import Path
import importlib.util
import re
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("mining_round_fixture", ROOT / "tools/Exercise-MapMining.py")
mining = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mining)
# This assault pane has a verified unobstructed approach for all three traces.
# The smaller *56 window has only 40 units of height; its frame stops a standing hull.
GLASS_MODEL = 62


class Run(mining.Run):
    def __init__(self):
        super().__init__()
        self.report["scope"] = __doc__
        self.command_at = 0

    def command(self, text):
        # Each RCON command sends a challenge and the command itself. ReHLDS's
        # default connectionless budget is 3 packets/s over 60 s. Pace the probe
        # below it instead of weakening the server's network protection.
        time.sleep(max(0, self.command_at + .8 - time.monotonic()))
        self.command_at = time.monotonic()
        return super().command(text)

    def target_state(self):
        answer = self.command("gc_mining_state " + str(self.slot))
        result = re.search(r"health=([-\d.]+) solid=(\d+)", answer)
        traces = re.search(r"ray=(\d+) standing=(\d+) crouching=(\d+)", answer)
        if not result or not traces:
            raise RuntimeError("Missing native target: " + answer)
        return float(result[1]), int(result[2]), *(int(v) for v in traces.groups())

    def restart(self, command="gc_mining_round restart"):
        before = self.peer.actors[self.slot]["life"]
        with self.peer.lock:
            self.peer.pose = None
        self.command(command)
        self.until(lambda: self.peer.actors.get(self.slot, {}).get("life", before) != before and not self.peer.frozen,
                   "Native round restart and real player respawn")
        self.until(lambda: "terminating=0" in self.command("gc_mining_state"), "Round termination cleared")

    def damage_glass(self, lethal=True):
        self.target(GLASS_MODEL)
        self.command("gc_mining_edit 3 1" if lethal else "gc_mining_edit 100 1")
        self.rested()
        result = self.request()
        self.check("real mining damage before round test " + str(self.event), result["status"] == 0)
        return result

    def end(self):
        revision = self.peer.policy[1]
        self.command("gc_mining_round end")
        self.until(lambda: self.peer.policy[1] > revision, "Round boundary invalidates mining requests")
        return revision

    def reconnect(self, previous_epoch=None):
        # Endpoint accepts at most one authority. A TCP connect can race the
        # frame that consumes the old peer's FIN; retry the fresh handshake.
        deadline = time.monotonic() + 30
        attempts = 0
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError("Native fixture exited during reconnect")
            candidate = None
            try:
                candidate = mining.Peer(self.config)
                attempts += 1
                while not candidate.error and not candidate.policy and time.monotonic() < deadline:
                    time.sleep(.03)
                if candidate.policy and candidate.world and candidate.world != previous_epoch:
                    self.peer = candidate
                    self.report.setdefault("reconnectAttempts", []).append(attempts)
                    return
            except OSError:
                pass
            if candidate:
                candidate.close()
            time.sleep(.1)
        raise TimeoutError("Authority reconnect did not produce the expected map policy")

    def run(self):
        self.start()
        self.check("persistence defaults to restore", '"mc_map_mining_persist" is "0"' in self.command("mc_map_mining_persist"))
        self.set_mode(1)
        self.target(GLASS_MODEL)
        original = self.target_state()
        self.check("fixture starts with native ray and standing/crouching collision", original[0] > 0 and original[1:] == (4, 1, 1, 1))
        self.damage_glass()
        self.check("mining actually removes native ray and standing/crouching collision", self.target_state()[1:] == (0, 0, 0, 0))
        old_revision = self.end()
        self.check("0 restores health and collision at round end before next spawn", self.target_state() == original)
        self.rested()
        self.check("pre-round requests cannot damage restored geometry", self.request(revision=old_revision)["status"] == 2)
        self.check("ended round rejects fresh mining", self.request()["status"] != 0)
        self.restart()

        self.command("mc_map_mining_persist 1")
        self.damage_glass(False)
        partial = self.target_state()
        self.end()
        self.check("1 retains partial mining at round end", self.target_state() == partial)
        self.restart()
        self.check("1 retains partial mining through native map cleanup", self.target_state() == partial)
        self.damage_glass()
        destroyed = self.target_state()
        self.end()
        self.check("1 keeps mined glass absent at round end", self.target_state() == destroyed)
        self.restart()
        self.check("1 prevents vanilla cleanup resurrecting mined glass", self.target_state() == destroyed)

        self.command("mc_map_mining_persist 0")
        self.check("hot change waits for the next boundary", self.target_state() == destroyed)
        self.command("gc_mining_round cancel")
        self.rested()
        self.check("canceled ReAPI round end does not restore the map", self.target_state() == destroyed
                   and "terminating=0" in self.command("gc_mining_state"))
        self.end()
        self.check("hot 1 to 0 restores earlier-round mining", self.target_state() == original)
        self.restart()

        self.command("mc_map_mining_persist 1")
        self.target(GLASS_MODEL)
        self.command("gc_mining_edit 3 1")
        self.command("gc_mining_bullet " + str(self.slot) + " 5")
        self.check("ordinary CS bullet really breaks unmined glass", self.target_state()[1] == 0)
        self.restart()
        self.check("1 does not change ordinary CS breakable reset", self.target_state() == original)

        self.target(GLASS_MODEL)
        self.command("gc_mining_edit 100 1")
        self.command("gc_mining_filter 1")
        self.rested()
        self.check("canceled native damage leaves no mining change", self.request()["status"] == 8)
        self.command("gc_mining_filter 0")
        self.restart()
        self.check("canceled damage cannot suppress later native reset", self.target_state() == original)

        self.damage_glass()
        self.command("mc_map_mining_persist 1.5")
        self.end()
        self.check("invalid persistence value restores rather than retaining", self.target_state() == original)
        self.restart()

        self.command("mc_map_mining_persist 1")
        self.damage_glass()
        self.restart()
        self.check("direct restart with no termination retains mining at 1", self.target_state()[1] == 0)
        self.command("mc_map_mining_persist 0")
        self.restart()
        self.check("direct restart with no termination restores mining at 0", self.target_state() == original)

        self.command("mc_map_mining_persist 1")
        self.damage_glass()
        self.restart("sv_restart 1")
        self.check("sv_restart preserves mining at 1", self.target_state()[1] == 0)
        self.command("mc_map_mining_persist 0")
        self.restart("sv_restart 1")
        self.check("sv_restart restores mining at 0", self.target_state() == original)

        self.command("mc_map_mining_persist 1")
        self.damage_glass()
        self.end()
        self.command("gc_mining_round restart_end")
        self.check("new round ending in the same frame still restores at 0", self.target_state() == original)
        self.restart()

        self.command("mc_map_mining_persist 1")
        self.damage_glass()
        destroyed = self.target_state()
        self.peer.close()
        self.peer = None
        self.reconnect()
        self.check("authority reconnect preserves current map mining", self.target_state() == destroyed)
        old_epoch = self.peer.world
        self.peer.close()
        self.peer = None
        self.command("changelevel cs_assault")
        self.reconnect(previous_epoch=old_epoch)
        answer = self.command("gc_mining_probe " + str(GLASS_MODEL))
        self.check("map change restores glass even with persistence 1", "solid=4" in answer
                   and '"mc_map_mining_persist" is "1"' in self.command("mc_map_mining_persist"))
        self.command("gc_mining_edit 3 1")
        self.command("gc_mining_round restart")
        self.check("new map cannot inherit old mining records", self.target_state() == original)
        self.report["runtimeErrors"] = {p.name: p.read_bytes()[self.error_offsets.get(p, 0):].decode("utf-8", errors="replace")
                                       for p in (mining.base.AMXX / "logs").glob("error_*.log") if p.stat().st_size > self.error_offsets.get(p, 0)}
        self.check("no AMXX or ReAPI runtime errors", not self.report["runtimeErrors"])
        self.report["passed"] = True


if __name__ == "__main__":
    run = Run()
    try:
        run.run()
    except Exception as exc:
        run.report.update(error=str(exc), passed=False)
        raise
    finally:
        run.close(ROOT / f"analysis/world-carving/mining-rounds-{time.time_ns()}.json")
    sys.exit(0 if run.report["passed"] else 1)
