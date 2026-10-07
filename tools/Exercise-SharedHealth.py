"""Real ReHLDS/Fabric health authority, form changes and one-shot death through ReAPI."""
import importlib.util
import json
import re
import time
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
def load(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/"tools"/file)
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value
runtime=load("vitals_runtime","Exercise-Renderer.py")
native=load("vitals_native","GoldSrc-Command.py")
DIM="goldcraft:cs_assault_f6725c06"

def main():
    game=runtime.Sandbox("cs-client-b")
    prefix=ROOT/"analysis/goldcraft-tests"/f"shared-vitals16-{int(time.time())}"
    report={"source":__doc__,"checks":{},"phases":{},"commands":game.commands,"nativeCommands":[]}
    original={};chest=False
    def cs(command):
        response=native.command(command);report["nativeCommands"].append({"command":command,"response":response});return response
    def host():return runtime.read_json(ROOT/"sandbox/cs-server/logs/goldcraft-server-status.json")
    def mc():return runtime.read_json(ROOT/"sandbox/cs-server/logs/minecraft-server-status.json")
    def actor():return next(a for a in host()["actors"] if a["slot"]==game.status()["playerSlot"])
    def player():return next(p for p in mc()["players"] if p["slot"]==actor()["slot"])
    def state():
        h=host();return {"actor":actor(),"player":player(),"hudHealth":game.status()["hudHealth"],
            "vitalsAccepted":h["vitalsAccepted"],"vitalsRejected":h["vitalsRejected"],"combat":cs(f"gc_test_combat_status #{actor()['userid']}")}
    def wait(predicate,label,seconds=12):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            result=predicate()
            if result:return result
            time.sleep(.05)
        raise TimeoutError(label)
    def check(label,value):
        report["checks"][label]=bool(value);print(json.dumps({"check":label,"passed":bool(value)}),flush=True)
        if not value:raise AssertionError(label)
    def synced(health):
        a=actor();p=player()
        return abs(a["health"]-health)<.02 and abs(p["health"]-health/5)<.005 and p["vitalsPending"]==0
    def baseline(health=100,armor=0):
        cs(f"gc_test_vitals #{actor()['userid']} {health} {armor}")
        wait(lambda:synced(health),"native fixture baseline mirrors to MC")
    def form(value):
        game.host(f"form {value}")
        wait(lambda:actor()["minecraftForm"]==bool(value) and game.status()["minecraftControl"]==bool(value),"form control")
        time.sleep(.3)
    def deaths():
        text=cs(f"gc_test_combat_status #{actor()['userid']}")
        return int(re.search(r"deaths=(\d+)",text)[1])
    def calls():
        text=cs(f"gc_test_combat_status #{actor()['userid']}")
        return int(re.search(r"calls=(\d+)",text)[1])
    def damage(amount):
        return game.minecraft(f"damage {game.player} {amount} minecraft:generic")
    def cvar(name):
        response=cs(name);match=re.search(r'is "([^"]+)"',response)
        if not match:raise RuntimeError(f"Cvar query failed: {name}")
        return match[1]
    def phase(name):report["phases"][name]=state()
    try:
        wait(lambda:game.status()["windowFocused"] and game.status()["inputActive"] and game.status()["minecraftControl"],"B controlled gameplay")
        report["before"]=state()
        original={"form":actor()["minecraftForm"],"mode":player()["mode"],"origin":actor()["origin"],"view":game.status()["viewAngles"],
            "health":actor()["health"],"armor":actor()["armor"]}
        original["cvars"]={name:cvar(name) for name in ("mp_forcerespawn","mp_round_infinite")}
        original["rules"]={}
        for rule in ("naturalRegeneration","keepInventory"):
            answer=game.minecraft(f"gamerule {rule}");original["rules"][rule]=re.search(r"(true|false)\s*$",answer)[1]
        cs("mp_forcerespawn 0");cs("mp_round_infinite 1")
        game.minecraft("gamerule naturalRegeneration false");game.minecraft("gamerule keepInventory true")
        form(1);game.minecraft(f"gamemode survival {game.player}")
        game.minecraft(f"effect clear {game.player}")
        # Preserve the two equipment slots using vanilla item-copy commands.
        created=game.minecraft(f"execute in {DIM} if block 0 319 0 minecraft:air run setblock 0 319 0 minecraft:barrel")
        if "Changed" not in created:raise RuntimeError("Reserved equipment backup location is occupied or unavailable")
        chest=True
        for index,slot in enumerate(("armor.chest","weapon.offhand")):
            game.minecraft(f"execute in {DIM} run item replace block 0 319 0 container.{index} from entity {game.player} {slot}")
            game.minecraft(f"item replace entity {game.player} {slot} with minecraft:air")
        form(0);baseline()
        birth=actor()["spawn"];before_calls=calls()
        cs(f"gc_test_damage #{actor()['userid']} 20")
        wait(lambda:synced(80),"native damage visible in spectator follower")
        check("CS damage updates paired spectator health",player()["mode"]=="spectator" and synced(80));phase("native_damage")
        for _ in range(2):form(1);form(0)
        form(1)
        wait(lambda:abs(game.status()["hudHealth"]-16)<.01,"actual MC HUD health")
        check("repeated form switches preserve native health and real spawn",synced(80) and actor()["spawn"]==birth)
        check("native damage was applied through one ReAPI damage callback",calls()==before_calls+1)
        before_calls=calls();damage(4)
        wait(lambda:synced(60),"MC damage committed by ReHLDS")
        check("vanilla MC damage reaches native health exactly once",calls()==before_calls+1 and synced(60));phase("minecraft_damage")
        game.minecraft(f"effect give {game.player} minecraft:instant_health 1 0 true")
        wait(lambda:synced(80),"MC healing committed by ReHLDS")
        check("vanilla MC healing reaches native health",synced(80));phase("healing")
        baseline(100,100)
        game.minecraft(f"item replace entity {game.player} armor.chest with minecraft:diamond_chestplate")
        time.sleep(.65);game.minecraft(f"damage {game.player} 4 minecraft:mob_attack")
        wait(lambda:actor()["health"]<100 and synced(actor()["health"]),"armored MC damage reconciliation")
        check("MC armor mitigates once without consuming a second CS armor layer",80<actor()["health"]<100 and actor()["armor"]==100);phase("armor")
        game.minecraft(f"item replace entity {game.player} armor.chest with minecraft:air")
        baseline();time.sleep(.65)
        before_calls=calls()
        damage(2);cs(f"gc_test_damage #{actor()['userid']} 15")
        wait(lambda:synced(75),"both engines' damage retained")
        check("MC and native damage both survive the same reconciliation window",synced(75) and calls()==before_calls+2);phase("concurrent_damage")
        baseline();time.sleep(.65)
        before_deaths=deaths()
        game.minecraft(f"item replace entity {game.player} weapon.offhand with minecraft:totem_of_undying")
        damage(100)
        wait(lambda:0<actor()["health"]<=10 and synced(actor()["health"]),"vanilla totem final health committed")
        check("vanilla totem prevents native death before final health commit",actor()["alive"] and deaths()==before_deaths);phase("totem")
        game.minecraft(f"effect clear {game.player}");baseline();time.sleep(.65)
        cs(f"gc_test_damage_filter #{actor()['userid']} 1")
        before_rejected=host()["vitalsRejected"];before_deaths=deaths()
        damage(100)
        wait(lambda:host()["vitalsRejected"]>before_rejected and synced(100),"ReAPI rejected lethal damage restores MC prediction")
        check("ReAPI can reject lethal MC damage without a death or inventory drop",player()["alive"] and deaths()==before_deaths);phase("plugin_rejects_lethal")
        cs(f"gc_test_damage_filter #{actor()['userid']} 0")
        wait(lambda:game.status()["minecraftControl"],"MC control after rejected death")
        game.minecraft(f"effect clear {game.player}");time.sleep(.7)
        before_deaths=deaths();birth=actor()["spawn"]
        game.minecraft(f"kill {game.player}")
        wait(lambda:not actor()["alive"] and not player()["alive"],"MC death kills native body")
        time.sleep(.7)
        check("MC lethal damage produces one native Killed callback",deaths()==before_deaths+1)
        check("MC cannot auto-respawn while the native body is dead",not actor()["alive"] and not player()["alive"] and actor()["spawn"]==birth)
        phase("confirmed_death")
        cs(f"gc_test_respawn #{actor()['userid']}")
        wait(lambda:actor()["spawn"]>birth and player()["alive"] and game.status()["minecraftControl"] and synced(100),"native respawn restores paired body",20)
        check("real native spawn restores health and the current Minecraft player",player()["handlerMatchesPlayer"] and player()["trackedEntityMatchesPlayer"]);phase("respawn")
        report["capture"]=game.capture(prefix.with_name(prefix.name+"-restored-hud.bmp"))
        check("restored HUD reports authority health with valid Renderer",game.status()["hudHealth"]==20 and game.status()["glError"]==0)
        report["outcome"]="passed"
    except Exception as error:
        report["outcome"]="incomplete";report["error"]=str(error)
        try:report["atFailure"]=state()
        except Exception as nested:report["stateError"]=str(nested)
    finally:
        try:
            if original:
                cs(f"gc_test_damage_filter #{actor()['userid']} 0")
                if not actor()["alive"]:cs(f"gc_test_respawn #{actor()['userid']}")
                wait(lambda:actor()["alive"],"cleanup living body")
                if chest:
                    for index,slot in enumerate(("armor.chest","weapon.offhand")):
                        game.minecraft(f"execute in {DIM} run item replace entity {game.player} {slot} from block 0 319 0 container.{index}")
                    game.minecraft(f"execute in {DIM} if block 0 319 0 minecraft:barrel run setblock 0 319 0 minecraft:air")
                form(0)
                cs(f"gc_test_position #{actor()['userid']} {' '.join(map(str,original['origin']))}")
                form(1);game.minecraft(f"gamemode {original['mode']} {game.player}")
                baseline(original["health"],original["armor"])
                form(int(original["form"]));game.host(f"view {original['view'][1]} {original['view'][0]}")
                for name,value in original.get("cvars",{}).items():cs(f"{name} {value}")
                for name,value in original.get("rules",{}).items():game.minecraft(f"gamerule {name} {value}")
            report["after"]=state()
        except Exception as error:report["cleanupError"]=str(error)
        game.connection.close();prefix.with_suffix(".json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps({"report":str(prefix.with_suffix(".json")),"outcome":report["outcome"],"checks":report["checks"],"error":report.get("error"),"cleanupError":report.get("cleanupError")},indent=2))
    return 0 if report["outcome"]=="passed" and "cleanupError" not in report else 1

if __name__=="__main__":raise SystemExit(main())
