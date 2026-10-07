"""Capture the real MC player from front/back/sides through CS B at a fixed observer pose."""
import argparse
import importlib.util
import json
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("renderer", ROOT / "tools/Exercise-Renderer.py")
renderer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(renderer)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", choices=("before", "after"), required=True)
    args = parser.parse_args()
    a, b = renderer.Sandbox(), renderer.Sandbox("cs-client-b")
    prefix = ROOT / "analysis/goldcraft-tests" / f"player-surfaces-{args.label}-{int(time.time())}"
    report = {"scope": __doc__, "label": args.label, "commands": a.commands, "captures": {}}
    prior = {}
    try:
        for key, sandbox, pos, yaw in (("A", a, "-11.5 59.001 70.125", 0), ("B", b, "-7.5 59.001 70.125", 180)):
            prior[key] = sandbox.status()
            if not prior[key]["minecraftControl"]:
                raise RuntimeError("Both live pairs must have Minecraft control")
            a.minecraft(f"execute as {sandbox.player} in {renderer.DIMENSION} run tp @s {pos}")
            sandbox.host(f"view {yaw} 0")
        for name, yaw in (("front", 0), ("back", 180), ("left", 90), ("right", -90)):
            a.minecraft(f"execute as {a.player} in {renderer.DIMENSION} run tp @s -11.5 59.001 70.125")
            a.host(f"view {yaw} 0")
            time.sleep(1.1)
            subject_before = a.status()
            captured = b.capture(prefix.with_name(prefix.name + f"-{name}.bmp"))
            captured["subject"] = a.status()
            captured["subjectBefore"] = subject_before
            captured["subjectStable"] = renderer.same_view(subject_before, captured["subject"])
            captured["subjectMatchesFixture"] = all(abs(x-y) < .003 for x,y in zip(captured["subject"]["minecraftFeet"], (-11.5,59,70.125))) and abs((captured["subject"]["viewAngles"][1]-yaw+180)%360-180) < .05 and abs(captured["subject"]["viewAngles"][0]) < .05
            captured["minecraftRotation"] = a.minecraft("data get entity GoldCraft_A Rotation")
            report["captures"][name] = captured
        report["checks"] = {
            "all_observer_views_fixed": all(c["stable"] and renderer.same_view(report["captures"]["front"]["after"], c["after"]) for c in report["captures"].values()),
            "all_GL_errors_zero": all(c["after"]["glError"] == 0 for c in report["captures"].values()),
            "one_avatar_per_view": all(c["after"]["avatarCount"] == 1 for c in report["captures"].values()),
            "all_subject_poses_match": all(c["subjectStable"] and c["subjectMatchesFixture"] for c in report["captures"].values()),
        }
        report["outcome"] = "matched captures ready for surface inspection" if all(report["checks"].values()) else "inconclusive"
    except Exception as error:
        report["outcome"] = "incomplete"
        report["error"] = str(error)
    finally:
        for key, sandbox in (("A", a), ("B", b)):
            if key in prior:
                state = prior[key]
                a.minecraft(f"execute as {sandbox.player} in {renderer.DIMENSION} run tp @s {' '.join(map(str, state['minecraftFeet']))}")
                sandbox.host(f"view {state['viewAngles'][1]} {state['viewAngles'][0]}")
        a.connection.close()
        b.connection.close()
        prefix.with_suffix(".json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"report": str(prefix.with_suffix('.json')), "outcome": report["outcome"], "checks": report.get("checks"), "error": report.get("error")}, indent=2))


if __name__ == "__main__":
    main()
