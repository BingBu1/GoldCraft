"""Exercise two real isolated CS/MC pairs against the same native and Minecraft servers."""
import importlib.util
import json
import math
import re
import time
from pathlib import Path

spec=importlib.util.spec_from_file_location("renderer_test",Path(__file__).with_name("Exercise-Renderer.py"))
renderer=importlib.util.module_from_spec(spec)
spec.loader.exec_module(renderer)
ROOT=renderer.ROOT
DIMENSION=renderer.DIMENSION


def vector(response):
    match=re.search(r"\[([^\]]+)\]",response)
    if not match:raise RuntimeError(response)
    return [float(v.strip().removesuffix("d").removesuffix("f")) for v in match.group(1).split(",")]


def delta(before,after):
    return [b-a for a,b in zip(before,after)]


def main():
    pairs={key:renderer.Sandbox(f"cs-client-{key.lower()}") for key in ("A","B")}
    command=pairs["A"].minecraft
    prefix=ROOT/"analysis/goldcraft-tests"/f"multiplayer-{int(time.time())}"
    report={"scope":"Two actual CS MetaHook clients, two real Fabric clients, ReHLDS dedicated server and Minecraft dedicated server. Tests use guarded native engine input commands, with RCON only for fixtures and authoritative observation.",
            "commands":pairs["A"].commands,"checks":{},"phases":[],"captures":{}}
    originals={};anchor_created=False

    def block_is(pos,block):
        return command(f"execute in {DIMENSION} if block {' '.join(map(str,pos))} minecraft:{block}").strip()=="Test passed"

    def sample():
        result={key:{"position":vector(command(f"data get entity {sandbox.player} Pos")),"native":sandbox.status()}
                for key,sandbox in pairs.items()}
        for key,sandbox in pairs.items():
            diagnostic=sandbox.logs/"minecraft-client-status.json"
            if diagnostic.exists():result[key]["minecraftInput"]=renderer.read_json(diagnostic)
        result["server"]=renderer.read_json(renderer.SERVER_STATUS)
        result["time"]=time.monotonic()
        return result

    def wait_block(pos,block):
        deadline=time.monotonic()+4
        while time.monotonic()<deadline:
            if block_is(pos,block):return True
            time.sleep(.05)
        return False

    def capture_phase(name):
        return {key:sandbox.capture(prefix.with_name(prefix.name+f"-{name}-{key}.bmp")) for key,sandbox in pairs.items()}

    try:
        initial=sample();report["initial"]=initial
        for key,sandbox in pairs.items():
            if not (initial[key]["native"]["minecraftControl"] and initial[key]["native"]["sceneApi"]):
                raise RuntimeError(f"Pair {key} is not ready")
            originals[key]={"position":initial[key]["position"],"angles":initial[key]["native"]["viewAngles"]}
        report["players"]=command("list uuids")
        ids={key:re.search(r"\[([^\]]+)\]",command(f"data get entity {sandbox.player} UUID")).group(1) for key,sandbox in pairs.items()}
        report["checks"]["distinctMinecraftIdentities"]=ids["A"]!=ids["B"]
        report["checks"]["sharedMapEpoch"]=initial["A"]["native"]["world"]==initial["B"]["native"]["world"]==initial["server"]["world"]
        identities=[json.loads((ROOT/f"sandbox/cs-client-{key.lower()}/instance.json").read_text(encoding="utf-8-sig")) for key in pairs]
        # Record equality checks, never the authentication tokens themselves.
        report["checks"].update({"different_"+field:identities[0][field]!=identities[1][field]
                                 for field in ("clientPort","clientSession","clientToken","profileId")})
        # Use the verified clear floor. A moves away first, leaving B's route clear.
        for key,x in (("A",-9.47015587),("B",-11.625)):
            command(f"execute as {pairs[key].player} in {DIMENSION} run tp @s {x} 59.001 70.125")
            pairs[key].host("view 0 0")
        time.sleep(.7)
        for key,other in (("A","B"),("B","A")):
            if key=="B" and not report["checks"].get("A_forward"):
                raise RuntimeError("A did not clear B's route; do not confuse physical pushing with input cross-talk")
            phase={"movingPair":key,"before":sample(),"samples":[]}
            pairs[key].host("forward 0.5")
            deadline=time.monotonic()+1.25
            while time.monotonic()<deadline:
                phase["samples"].append(sample());time.sleep(.06)
            phase["after"]=sample()
            phase["deltas"]={k:delta(phase["before"][k]["position"],phase["after"][k]["position"]) for k in pairs}
            moved=phase["deltas"][key]
            report["checks"][key+"_forward"]=1.5<moved[0]<3 and abs(moved[1])<.01 and abs(moved[2])<.01
            report["checks"][key+"_does_not_move_"+other]=math.dist(phase["before"][other]["position"],phase["after"][other]["position"])<.003
            report["phases"].append(phase)
        # Native server diagnostic snapshots publish every 500 ms; wait after the last motion.
        time.sleep(.7)
        state=sample();report["authority"]=state
        mapped=[]
        for key in pairs:
            x,y,z=state[key]["position"];expected=[x*32,-z*32,(y-64)*32+36]
            matches=[a["slot"] for a in state["server"]["actors"] if a["controlled"] and math.dist(a["origin"],expected)<.1]
            report["checks"][key+"_authoritative_host_pose"]=len(matches)==1
            mapped+=matches
        report["checks"]["distinctHostSlots"]=len(mapped)==2 and len(set(mapped))==2

        # Never replace existing construction. Only B's fresh development inventory is used.
        anchor=(-6,60,70);placed=(-7,60,70)
        if not block_is(anchor,"air") or not block_is(placed,"air"):
            raise RuntimeError("Construction occupies the reserved fixture; no blocks were changed")
        command(f"execute in {DIMENSION} run setblock -6 60 70 minecraft:stone keep")
        anchor_created=True
        command(f"execute as GoldCraft_A in {DIMENSION} run tp @s -9.5 59.001 73.5")
        command(f"execute as GoldCraft_B in {DIMENSION} run tp @s -9.5 59.001 70.5")
        pairs["A"].host("view 45 0");pairs["B"].host("view 0 0")
        command("item replace entity GoldCraft_B weapon.mainhand with minecraft:lime_concrete 1")
        time.sleep(1.5)
        report["captures"]["before"]=capture_phase("before")
        report["placementBefore"]=sample()
        pairs["B"].host("attack2 0.12")
        report["checks"]["B_places_block_from_CS"]=wait_block(placed,"lime_concrete")
        time.sleep(.8)
        report["placementAfter"]=sample()
        report["captures"]["placed"]=capture_phase("placed")
        for key in pairs:
            before=report["placementBefore"][key]["native"];after=report["placementAfter"][key]["native"]
            report["checks"][key+"_receives_placed_mesh"]=after["vertices"]>before["vertices"] and after["glError"]==0
            report["checks"][key+"_matched_place_view"]=renderer.same_view(before,after)
        if not report["checks"]["B_places_block_from_CS"]:
            raise RuntimeError("Real CS right-click did not place the expected block")
        pairs["B"].host("attack 0.12")
        report["checks"]["B_breaks_block_from_CS"]=wait_block(placed,"air")
        time.sleep(.8)
        report["removalAfter"]=sample();report["captures"]["removed"]=capture_phase("removed")
        for key in pairs:
            before=report["placementBefore"][key]["native"];after=report["removalAfter"][key]["native"]
            report["checks"][key+"_receives_removed_mesh"]=after["vertices"]==before["vertices"] and after["glError"]==0
        report["outcome"]="passed" if all(report["checks"].values()) else "failed or inconclusive; inspect per-check evidence"
    except Exception as error:
        report["error"]=str(error);report["outcome"]="incomplete"
    finally:
        if anchor_created:
            command(f"execute in {DIMENSION} if block -6 60 70 minecraft:stone run setblock -6 60 70 minecraft:air")
            command(f"execute in {DIMENSION} if block -7 60 70 minecraft:lime_concrete run setblock -7 60 70 minecraft:air")
            command("item replace entity GoldCraft_B weapon.mainhand with minecraft:air")
        for key,original in originals.items():
            command(f"execute as {pairs[key].player} in {DIMENSION} run tp @s {' '.join(map(str,original['position']))}")
            pairs[key].host(f"view {original['angles'][1]} {original['angles'][0]}")
        for sandbox in pairs.values():sandbox.connection.close()
        prefix.with_suffix(".json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps({"report":str(prefix.with_suffix('.json')),"outcome":report["outcome"],"checks":report["checks"],"error":report.get("error")},indent=2))


if __name__=="__main__":main()
