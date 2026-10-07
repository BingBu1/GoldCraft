"""Actual sandbox B pistol input, vanilla wolf revenge and hostile attacks on a paired CS-form player."""
import importlib.util
import json
import math
import re
import time
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
def load(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/"tools"/file)
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value
runtime=load("mob_runtime","Exercise-Renderer.py")
native=load("mob_native","GoldSrc-Command.py")
DIM="goldcraft:cs_assault_f6725c06"
TAG="goldcraft_paired15_combat"


def main():
    game=runtime.Sandbox("cs-client-b")
    prefix=ROOT/"analysis/goldcraft-tests"/f"paired-mobs15-{int(time.time())}"
    report={"source":__doc__,"checks":{},"phases":{},"commands":game.commands,"nativeCommands":[]}
    original={}
    def cs(command):
        response=native.command(command);report["nativeCommands"].append({"command":command,"response":response});return response
    def server():return runtime.read_json(ROOT/"sandbox/cs-server/logs/goldcraft-server-status.json")
    def mcserver():return runtime.read_json(ROOT/"sandbox/cs-server/logs/minecraft-server-status.json")
    def actor():return next(a for a in server()["actors"] if a["slot"]==game.status()["playerSlot"])
    def state():return {"native":game.status(),"actor":actor(),"server":server(),"mcServer":mcserver(),"mc":runtime.read_json(game.logs/"minecraft-client-status.json")}
    def wait(predicate,label,seconds=12):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            value=predicate()
            if value:return value
            time.sleep(.05)
        raise TimeoutError(label)
    def check(label,value):
        report["checks"][label]=bool(value)
        if not value:raise AssertionError(label)
    def form(value):
        game.host(f"form {value}")
        wait(lambda:actor()["minecraftForm"]==bool(value) and game.status()["minecraftForm"]==bool(value),"form acknowledged")
        wait(lambda:game.status()["minecraftControl"]==bool(value),"control lease updated")
        time.sleep(.4)
    def place():
        cs(f"gc_test_position #{actor()['userid']} 496 176 36.03125")
        wait(lambda:abs(actor()["origin"][0]-496)<.1,"native fixture position")
        game.host("view 180 16.7")
    def data(field):return game.minecraft(f"execute in {DIM} run data get entity @e[tag={TAG},limit=1] {field}")
    def health():
        match=re.search(r":\s*(-?[0-9.]+)f\s*$",data("Health"));return float(match[1]) if match else 0
    def anger():
        match=re.search(r":\s*(\d+)\s*$",data("AngerTime"));return int(match[1]) if match else 0
    def position():
        match=re.search(r"\[([^\[\]]+)\]\s*$",data("Pos"))
        if not match:raise RuntimeError("Test mob position unavailable")
        return [float(v.strip().rstrip("d")) for v in match[1].split(",")]
    def clear():game.minecraft(f"execute in {DIM} run kill @e[tag={TAG}]")
    def capture(name):return game.capture(prefix.with_name(prefix.name+"-"+name+".bmp"))

    try:
        wait(lambda:game.status()["windowFocused"] and game.status()["inputActive"],"B gameplay foreground")
        report["before"]=state();original={"form":actor()["minecraftForm"],"origin":actor()["origin"],"view":game.status()["viewAngles"]}
        if actor()["health"]<60:raise RuntimeError("B needs at least 60 native health before this bounded combat test")
        form(0);place()
        check("paired player uses native CS body and Fabric spectator follower",actor()["uuid"]!="0"*32 and not actor()["controlled"] and not game.status()["viewModelSuppressed"])
        cs(f"gc_test_equip #{actor()['userid']} weapon_usp");time.sleep(1.2)
        clear()
        game.minecraft(f'execute in {DIM} run summon minecraft:wolf 10.5 64.001 -5.5 {{Tags:["{TAG}"],PersistenceRequired:1b,NoAI:1b}}')
        wait(lambda:health()>0,"test wolf spawned")
        time.sleep(.25);start=position();before_wolf=health();before_native=actor()["health"];accepted=server()["mobDamageAccepted"]
        game.host("attack .08")
        wait(lambda:0<health()<before_wolf,"real CS pistol damages wolf")
        after_shot=health()
        check("actual CS pistol input damages wolf",0<after_shot<before_wolf)
        game.minecraft(f'execute in {DIM} run data merge entity @e[tag={TAG},limit=1] {{NoAI:0b}}')
        wait(lambda:anger()>0,"vanilla wolf anger")
        check("wolf anger identifies the CS-form attacker",anger()>0)
        report["phases"]["wolf_before_chase"]={"position":start,"healthBefore":before_wolf,"healthAfterShot":after_shot,"state":state()}
        wait(lambda:actor()["health"]<before_native,"wolf chase and native bite",25)
        wolf=state();finish=position();report["phases"]["wolf_bite"]={"position":finish,"state":wolf,"capture":capture("wolf-bite")}
        check("wolf navigates over the CS map to the attacker",math.dist(start,finish)>1.5)
        check("wolf bite reduces real native health and is accepted",wolf["actor"]["health"]<before_native and wolf["server"]["mobDamageAccepted"]>accepted)
        clear();time.sleep(.3);place()
        before_native=actor()["health"];accepted=server()["mobDamageAccepted"]
        game.minecraft(f'execute in {DIM} run summon minecraft:zombie 10.5 64.001 -5.5 {{Tags:["{TAG}"],PersistenceRequired:1b,IsBaby:0b,ArmorItems:[{{}},{{}},{{}},{{id:"minecraft:iron_helmet",count:1}}]}}')
        wait(lambda:health()>0,"test zombie spawned")
        start=position()
        wait(lambda:actor()["health"]<before_native,"proactive zombie chase and native damage",25)
        zombie=state();finish=position();report["phases"]["zombie_bite"]={"start":start,"finish":finish,"state":zombie,"capture":capture("zombie-bite")}
        check("hostile mob proactively pursues a CS-form player",math.dist(start,finish)>1.5)
        check("hostile damage reaches native player without prior attack",zombie["actor"]["health"]<before_native and zombie["server"]["mobDamageAccepted"]>accepted)
        before_a=next(a for a in report["before"]["server"]["actors"] if a["slot"]!=actor()["slot"])
        after_a=next(a for a in zombie["server"]["actors"] if a["slot"]==before_a["slot"])
        check("B mob damage does not leak into A health",after_a["health"]==before_a["health"])
        check("mob presentation retains native CS weapon and valid Renderer",zombie["native"]["glError"]==0 and zombie["native"]["hostViewModel"]==1)
        report["outcome"]="passed"
    except Exception as error:
        report["outcome"]="incomplete";report["error"]=str(error);report["atFailure"]=state()
    finally:
        try:
            clear()
            if original and actor()["alive"]:
                if actor()["minecraftForm"]:form(0)
                cs(f"gc_test_position #{actor()['userid']} {' '.join(map(str,original['origin']))}")
                form(int(original["form"]))
                game.host(f"view {original['view'][1]} {original['view'][0]}")
            report["after"]=state()
            report["healthHandoffObservation"]={"nativeHealth":actor()["health"],"minecraftHealth":game.status()["hudHealth"],"expectedMcIfShared":actor()["health"]/5,
                "note":"Observation only: authoritative form health transfer remains a separate incomplete requirement."}
        except Exception as error:report["cleanupError"]=str(error)
        game.connection.close();prefix.with_suffix(".json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps({"report":str(prefix.with_suffix('.json')),"outcome":report["outcome"],"checks":report["checks"],"error":report.get("error"),"cleanupError":report.get("cleanupError")},indent=2))
    return 0 if report["outcome"]=="passed" and "cleanupError" not in report else 1


if __name__=="__main__":raise SystemExit(main())
