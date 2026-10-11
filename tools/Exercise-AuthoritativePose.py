"""Check compact Epic-style bodies on the real isolated ReHLDS authority path.

Uses the existing headless fixture and authenticated synthetic authority peer.
It does not operate the human's B client or represent a Minecraft animation test.
Changed fixture files are restored and only the owned server is stopped.
"""
from pathlib import Path
import importlib.util
import struct
import time

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("pose_rounds", ROOT / "tools/Exercise-MapMiningRounds.py")
rounds = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rounds)


class Run(rounds.Run):
    def __init__(self):
        super().__init__()
        self.report["scope"] = __doc__

    def dimensions(self):
        return self.snapshot()["authoritativeDimensions"]

    def snapshot(self):
        return self.until(self.state, "complete current native status")

    def submit(self, width=.6, height=1.8, eye=1.62, flags=3, life=None):
        actor = self.peer.actors[self.slot]
        head = struct.pack("<III", self.slot, actor["serial"], actor["life"] if life is None else life) + self.uuid
        tail = struct.pack("<I11f", flags, *self.feet, 0, 0, 0, 0, 0, eye, width, height)
        with self.peer.lock:
            self.peer.pose = head, tail

    def controlled(self):
        return bool(self.peer.actors[self.slot]["flags"] & 32)

    def checkpoint(self):
        # A status write may lag the wire snapshot that first reports control.
        def acquired():
            state = self.state()
            life = self.peer.actors[self.slot]["life"]
            return state if any(p["slot"] == self.slot and p["controlled"] and p["life"] == life
                                for p in state["actors"]) else None
        state = self.until(acquired, "acquisition in status trace")
        return (self.peer.world, self.peer.connection, self.peer.actors[self.slot]["life"],
                state["serverControlTrace"]["events"], state["bridgeGeneration"])

    def expired(self, before, label):
        self.until(lambda: not self.controlled(), label, seconds=4)
        world, connection, life, sequence, generation = before
        def release():
            records = self.state()["serverControlTrace"]["records"]
            return next((r for r in records if r["sequence"] > sequence and r["slot"] == self.slot), None)
        event = self.until(release, label + " trace")
        self.report.setdefault("expiryChecks", []).append({"label": label, "before": before, "event": event})
        self.check(label + " uses unchanged timeout and identity", event["cause"] == 10
                   and 1000 <= event["poseMs"] < 2000 and int(event["world"]) == world
                   and event["connection"] == generation and event["life"] == life
                   and self.peer.world == world and self.peer.connection == connection
                   and self.peer.actors[self.slot]["life"] == life)

    def run(self):
        self.start()
        player = next(p for p in self.snapshot()["actors"] if p["slot"] == self.slot)
        x, y, z = player["origin"]
        self.feet = x / 32, (z + player["mins"][2]) / 32 + 64, -y / 32
        self.submit()
        self.until(self.controlled, "standing authority lease")
        self.check("standing player is controlled", self.controlled())
        before = self.dimensions()
        self.submit(height=.8)
        self.until(lambda: self.dimensions()["eyeAboveBody"] > before["eyeAboveBody"], "compact body accepted")
        samples = []
        deadline = time.monotonic() + 2.2
        while time.monotonic() < deadline:
            samples.append(dict(controlled=self.controlled(), life=self.peer.actors[self.slot]["life"]))
            time.sleep(.05)
        after = self.dimensions()
        self.report["compactSamples"] = samples
        self.check("compact body keeps lease beyond timeout", all(p["controlled"] for p in samples))
        self.check("compact poses keep native life", len({p["life"] for p in samples}) == 1)
        self.check("compact frames are accepted without rejection", after["eyeAboveBody"] >= before["eyeAboveBody"] + 15
                   and after["rejected"] == before["rejected"])

        for name, args, reason in (
            ("unknown flags", {"flags": 9}, 1),
            ("oversized width", {"width": 4.01}, 3),
            ("undersized height", {"height": .09}, 4),
            ("oversized eye", {"eye": 4.11}, 5),
            ("negative eye", {"eye": -.01}, 5),
        ):
            old = self.dimensions()["rejected"]
            checkpoint = self.checkpoint()
            self.submit(**args)
            rejected = self.until(lambda: (d if (d := self.dimensions())["rejected"] > old else None), name)
            self.check(name + " is rejected", rejected["lastReason"] == reason)
            accepted = rejected["accepted"]
            self.expired(checkpoint, name + " cannot renew authority")
            self.check(name + " never refreshes accepted pose", self.dimensions()["accepted"] == accepted)
            self.submit()
            self.until(lambda: self.dimensions()["accepted"] > accepted, "valid pose recovers after " + name)
            self.until(self.controlled, "control recovers after " + name)

        checkpoint = self.checkpoint()
        old_stale = self.snapshot()["staleLifePoses"]
        self.submit(height=.8, life=self.peer.actors[self.slot]["life"] + 1)
        # Wait for a status written after rejection; no valid pose remains queued.
        stale = self.until(lambda: (s if (s := self.state())["staleLifePoses"] > old_stale else None),
                           "stale life rejected in native status")
        valid = stale["authoritativeDimensions"]["accepted"]
        self.expired(checkpoint, "stale-life poses cannot renew lease")
        self.check("stale life cannot refresh accepted pose", self.dimensions()["accepted"] == valid)
        self.submit(height=.8)
        self.until(self.controlled, "valid compact pose reacquires lease")
        checkpoint = self.checkpoint()
        with self.peer.lock:
            self.peer.pose = None
        self.expired(checkpoint, "silent authority releases at existing timeout")
        self.check("timeout still releases authority", not self.controlled())
        self.report["finalDimensions"] = self.dimensions()
        self.report["serverControlTrace"] = self.snapshot()["serverControlTrace"]
        self.report["passed"] = True


if __name__ == "__main__":
    run = Run()
    try:
        run.run()
    except Exception as failure:
        run.report.update(passed=False, error=str(failure))
        raise
    finally:
        output = ROOT / "analysis/goldcraft-tests/authoritative-pose.json"
        output.parent.mkdir(parents=True, exist_ok=True)
        run.close(output=output)
