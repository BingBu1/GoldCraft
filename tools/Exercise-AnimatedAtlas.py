"""Record real CS fire animation frames and native atlas counters in the saved sandbox fixture."""
import argparse
import importlib.util
import json
import time
from pathlib import Path

spec=importlib.util.spec_from_file_location("renderer_test",Path(__file__).with_name("Exercise-Renderer.py"))
renderer=importlib.util.module_from_spec(spec)
spec.loader.exec_module(renderer)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames",type=int,default=12,choices=range(2,31))
    parser.add_argument("--position",nargs=3,type=float,default=(1.25,60.001,72))
    parser.add_argument("--view",nargs=2,type=float,default=(-48,16),metavar=("YAW","PITCH"))
    parser.add_argument("--resync",action="store_true",help="Invalidate the native atlas and verify automatic scene replay")
    args=parser.parse_args()
    sandbox=renderer.Sandbox()
    prefix=renderer.ROOT/"analysis/goldcraft-tests"/f"fire-after-{int(time.time())}"
    report={"commands":sandbox.commands,"captures":[],"scope":"Actual persistent Minecraft fire on netherrack, rendered in CS; no generated animation frames"}
    try:
        if not sandbox.status()["minecraftControl"]:raise RuntimeError("Minecraft control is not ready")
        report["fixture"]=sandbox.minecraft(f"execute in {renderer.DIMENSION} if block 3 60 74 minecraft:fire run time query gametime")
        if "Test failed" in report["fixture"]:raise RuntimeError("The saved fire fixture is missing")
        if args.resync:
            initial=sandbox.status();started=time.monotonic()
            sandbox.host("scene_resync")
            recovery={"before":initial,"samples":[]};report["recovery"]=recovery
            while time.monotonic()-started<10:
                current=sandbox.status();recovery["samples"].append(current)
                if (current["world"]==initial["world"] and current["atlasGeneration"]>initial["atlasGeneration"]
                    and current["atlasAnimationPackets"]>initial["atlasAnimationPackets"]+10
                    and current["vertices"]>0 and current["minecraftControl"]):
                    recovery["seconds"]=time.monotonic()-started;recovery["after"]=current
                    break
                time.sleep(.1)
            else:raise RuntimeError("Native scene replay did not recover atlas animation and geometry")
        sandbox.minecraft(f"execute as GoldCraft_A in {renderer.DIMENSION} run tp @s {' '.join(map(str,args.position))}")
        sandbox.host(f"view {args.view[0]} {args.view[1]}")
        time.sleep(1.2)
        began=time.monotonic();report["before"]=sandbox.status()
        for index in range(args.frames):
            capture=sandbox.capture(prefix.with_name(prefix.name+f"-{index:02d}.bmp"))
            capture["time"]=time.time();report["captures"].append(capture)
            time.sleep(.1)
        report["seconds"]=time.monotonic()-began;report["after"]=sandbox.status()
        report["matchedView"]=all(c["stable"] and renderer.same_view(report["before"],c["after"]) for c in report["captures"])
        elapsed=report["seconds"]
        report["animationPacketsPerSecond"]=(report["after"]["atlasAnimationPackets"]-report["before"]["atlasAnimationPackets"])/elapsed
        report["animationBytesPerSecond"]=(report["after"]["atlasPatchBytes"]-report["before"]["atlasPatchBytes"])/elapsed
        report["fullAtlasUploads"]=report["after"]["atlasUploads"]-report["before"]["atlasUploads"]
    finally:
        sandbox.connection.close();prefix.with_suffix(".json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps({"report":str(prefix.with_suffix('.json')),**{k:report[k] for k in ("matchedView","animationPacketsPerSecond","animationBytesPerSecond","fullAtlasUploads")}},indent=2))


if __name__=="__main__":main()
