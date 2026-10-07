"""Measure delivered moving-mob geometry and recover its textures after a real Minecraft resource reload."""
import csv
import importlib.util
import json
import math
import re
import shutil
import statistics
import time
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
def load(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/"tools"/file)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result
r=load("entity_runtime","Exercise-Renderer.py")
native=load("entity_native","GoldSrc-Command.py")
DIM="goldcraft:cs_assault_f6725c06"
TAG="goldcraft_entity15_motion"

def main():
    g=r.Sandbox("cs-client-b")
    prefix=ROOT/"analysis/goldcraft-tests"/f"entity-presentation15-{int(time.time())}"
    report={"source":__doc__,"checks":{},"commands":g.commands,"nativeCommands":[],"samples":[]}
    original={}
    def cs(command):
        value=native.command(command);report["nativeCommands"].append({"command":command,"response":value});return value
    def actor():
        data=r.read_json(ROOT/"sandbox/cs-server/logs/goldcraft-server-status.json")
        return next(a for a in data["actors"] if a["slot"]==g.status()["playerSlot"])
    def mc():return r.read_json(g.logs/"minecraft-client-status.json")
    def wait(predicate,label,seconds=10):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            result=predicate()
            if result:return result
            time.sleep(.05)
        raise TimeoutError(label)
    def check(label,value):
        report["checks"][label]=bool(value)
        if not value:raise AssertionError(label)
    def form(value):
        g.host(f"form {value}");wait(lambda:actor()["minecraftForm"]==bool(value) and g.status()["minecraftControl"]==bool(value),"form state");time.sleep(.4)
    def position():
        result=g.minecraft(f"execute in {DIM} run data get entity @e[tag={TAG},limit=1] Pos")
        match=re.search(r"\[([^\[\]]+)\]\s*$",result)
        return [float(p.strip().rstrip("d")) for p in match[1].split(",")]
    def capture(label):return g.capture(prefix.with_name(prefix.name+"-"+label+".bmp"))
    def clear():g.minecraft(f"execute in {DIM} run kill @e[tag={TAG}]")
    try:
        wait(lambda:g.status()["windowFocused"] and g.status()["inputActive"],"B foreground")
        original={"form":actor()["minecraftForm"],"origin":actor()["origin"],"view":g.status()["viewAngles"]}
        g.host("menu_close");form(0)
        cs(f"gc_test_position #{actor()['userid']} 496 176 36.03125");g.host("view 180 16.7")
        cs(f"gc_test_equip #{actor()['userid']} weapon_usp");time.sleep(1.2)
        clear()
        g.minecraft(f'execute in {DIM} run summon minecraft:wolf 10.5 64.001 -5.5 {{Tags:["{TAG}"],PersistenceRequired:1b,NoAI:1b}}')
        time.sleep(.5)
        g.host("attack .08")
        g.minecraft(f'execute in {DIM} run data merge entity @e[tag={TAG},limit=1] {{NoAI:0b}}')
        def angry():
            response=g.minecraft(f"execute in {DIM} run data get entity @e[tag={TAG},limit=1] AngerTime")
            match=re.search(r":\s*(\d+)\s*$",response);return match and int(match[1])>0
        wait(angry,"wolf acquired native attacker")
        # Use the real command and verify it; do not assume version-specific NBT
        # attribute names. Slow the fixture only to obtain a longer walking sample.
        g.minecraft(f"execute in {DIM} run attribute @e[tag={TAG},limit=1] minecraft:generic.movement_speed base set 0.16")
        attribute=g.minecraft(f"execute in {DIM} run attribute @e[tag={TAG},limit=1] minecraft:generic.movement_speed base get")
        if not re.search(r"\b0\.16\b",attribute):raise RuntimeError("Movement-speed fixture was not applied")
        # Anger changes the real wolf texture once. That legitimate first upload
        # belongs to scene preparation, not the steady-movement cache test.
        time.sleep(.6)
        report["motionStart"]=position();report["before"]={"native":g.status(),"mc":mc()}
        g.host("entity_profile 8")
        start=time.monotonic();next_position=0;stopped=False
        while time.monotonic()-start<8.5:
            elapsed=time.monotonic()-start
            report["samples"].append({"time":elapsed,"native":g.status(),"mc":mc()})
            if elapsed>=next_position:
                p=position();report.setdefault("positions",[]).append({"time":elapsed,"position":p});next_position=elapsed+.5
                if not stopped and p[0]>13:
                    g.minecraft(f'execute in {DIM} run data merge entity @e[tag={TAG},limit=1] {{NoAI:1b}}');stopped=True
            time.sleep(.05)
        g.minecraft(f'execute in {DIM} run data merge entity @e[tag={TAG},limit=1] {{NoAI:1b}}')
        report["motionEnd"]=position();report["afterMotion"]={"native":g.status(),"mc":mc()}
        csv_path=g.logs/"goldcraft-entities.csv";shutil.copy2(csv_path,prefix.with_suffix(".csv"))
        rows=list(csv.DictReader(csv_path.open(newline="")))
        groups={}
        for row in rows:
            key=(row["texture"],row["flags"],row["vertices"])
            groups.setdefault(key,[]).append({k:float(v) for k,v in row.items()})
        # Other players stay still; select the mesh batch with the greatest X
        # travel and confirm direction/distance against the real wolf positions.
        selected=max(groups.values(),key=lambda rows:max(v["center_x"] for v in rows)-min(v["center_x"] for v in rows))
        low=min(v["center_x"] for v in selected);high=max(v["center_x"] for v in selected)
        moving=[v for v in selected if low+(high-low)*.15<v["center_x"]<low+(high-low)*.85]
        intervals=[b["time"]-a["time"] for a,b in zip(moving,moving[1:])]
        steps=[abs(b["center_x"]-a["center_x"]) for a,b in zip(moving,moving[1:])]
        times=sorted({v["time"] for values in groups.values() for v in values})
        metrics={"deliveredFrames":len(times),"deliveryFps":(len(times)-1)/(times[-1]-times[0]),
                 "meshTexture":int(selected[0]["texture"]),"meshVertices":int(selected[0]["vertices"]),
                 "meshTravelGoldSrcUnits":high-low,"movingFrames":len(moving),
                 "heldFraction":sum(x<.005 for x in steps)/len(steps),
                 "frameIntervalMedianMs":statistics.median(intervals)*1000,
                 "frameIntervalP95Ms":sorted(intervals)[int(len(intervals)*.95)]*1000,
                 "meanStepGoldSrcUnits":statistics.mean(steps),"maxStepGoldSrcUnits":max(steps)}
        report["metrics"]=metrics
        check("real moving wolf geometry travels over one Minecraft block",report["motionEnd"][0]-report["motionStart"][0]>1 and high-low>32)
        check("native receives over 40 entity geometry frames per second",metrics["deliveryFps"]>40)
        check("interpolated mesh moves on at least 80 percent of sampled frames",len(moving)>40 and metrics["heldFraction"]<.2)
        before=report["before"];after=report["afterMotion"]
        check("steady movement sends no repeated texture uploads",before["native"]["entityTextureUploads"]==after["native"]["entityTextureUploads"])
        check("moving entity delivery has no send failures",before["mc"]["entityExport"]["sendFailures"]==after["mc"]["entityExport"]["sendFailures"])
        report["beforeReloadCapture"]=capture("before-reload")
        began=time.monotonic();g.host("resource_reload")
        wait(lambda:g.status()["atlasGeneration"]>after["native"]["atlasGeneration"] and mc()["overlay"]=="" and g.status()["entityFrames"]>after["native"]["entityFrames"]+10,"resource reload and entity recovery",30)
        report["reloadSeconds"]=time.monotonic()-began
        report["afterReload"]={"native":g.status(),"mc":mc(),"capture":capture("after-reload")}
        check("real resource reload reuploads and restores entity textures",g.status()["entityTextures"]>0 and g.status()["entityTextureUploads"]>after["native"]["entityTextureUploads"] and g.status()["glError"]==0)
        time.sleep(2);post={"native":g.status(),"mc":mc()};report["settled"]=post
        check("entity textures stop reuploading after reload settles",post["native"]["entityTextureUploads"]==report["afterReload"]["native"]["entityTextureUploads"])
        check("native CS camera stays fixed during moving-mob measurement",all(max(abs(a-b) for a,b in zip(s["native"]["viewAngles"],report["samples"][0]["native"]["viewAngles"]))<.05 for s in report["samples"]))
        report["outcome"]="passed"
    except Exception as error:
        report["outcome"]="incomplete";report["error"]=str(error);report["atFailure"]={"native":g.status(),"mc":mc()}
    finally:
        try:
            clear()
            if original and actor()["alive"]:
                if actor()["minecraftForm"]:form(0)
                cs(f"gc_test_position #{actor()['userid']} {' '.join(map(str,original['origin']))}")
                form(int(original["form"]));g.host(f"view {original['view'][1]} {original['view'][0]}")
        except Exception as error:report["cleanupError"]=str(error)
        g.connection.close();prefix.with_suffix(".json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps({"report":str(prefix.with_suffix('.json')),"outcome":report["outcome"],"metrics":report.get("metrics"),"checks":report["checks"],"reloadSeconds":report.get("reloadSeconds"),"error":report.get("error"),"cleanupError":report.get("cleanupError")},indent=2))
    return 0 if report["outcome"]=="passed" and "cleanupError" not in report else 1

if __name__=="__main__":raise SystemExit(main())
