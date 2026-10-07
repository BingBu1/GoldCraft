"""Exercise actual GoldSrc Key_Event -> MetaHook -> Minecraft input in sandbox B.

The fixture enters the engine's public key API, not GoldCraft's UI/control handlers.
Windows input delivery is separately recorded by hosted-input15-acceptance.json.
"""
import importlib.util
import json
import re
import time
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location("key_runtime",ROOT/"tools/Exercise-Renderer.py")
runtime=importlib.util.module_from_spec(spec);spec.loader.exec_module(runtime)
DIM="goldcraft:cs_assault_f6725c06"


def main():
    game=runtime.Sandbox("cs-client-b")
    prefix=ROOT/"analysis/goldcraft-tests"/f"hosted-keys15-{int(time.time())}"
    report={"source":__doc__,"checks":{},"phases":{},"commands":game.commands}
    original={};created_block=False;created_slot=None

    def snapshot():
        return {"native":game.status(),"mc":runtime.read_json(game.logs/"minecraft-client-status.json"),
                "a":runtime.read_json(ROOT/"sandbox/cs-client-a/logs/goldcraft-client-status.json")}
    def wait(predicate,label,seconds=8):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            s=snapshot()
            if predicate(s):return s
            time.sleep(.05)
        raise TimeoutError(label)
    def check(label,result):
        report["checks"][label]=bool(result)
        if not result:raise AssertionError(label)
    def key(code):game.host(f"engine_key {code}")
    def screen(opened):return wait(lambda s:bool(s["mc"]["screen"])==opened and s["native"]["uiActive"]==opened,"inventory state")
    def number(field):
        response=game.minecraft(f"data get entity {game.player} {field}")
        return float(re.search(r":\s*(-?[0-9.]+)[a-z]?\s*$",response).group(1))
    def capture(label):return game.capture(prefix.with_name(prefix.name+"-"+label+".bmp"))

    try:
        s=wait(lambda s:s["native"]["minecraftControl"] and s["native"]["inputActive"],"active MC form")
        if s["native"].get("hostUiKeyboard") or s["native"].get("hostGameMenu"):
            raise RuntimeError("Close the actual CS MOTD/menu before gameplay key acceptance")
        report["before"]=s
        original={"mode":int(number("playerGameType")),"position":s["mc"]["position"],
                  "slot":s["mc"]["selectedSlot"],"view":s["native"]["viewAngles"]}
        game.host("menu_close");screen(False)
        game.minecraft(f"gamemode survival {game.player}")
        began=time.monotonic();key(105);opened=screen(True)
        report["phases"]["open"]={"seconds":time.monotonic()-began,"state":opened,"capture":capture("survival-inventory")}
        check("I opens the real survival inventory through GoldSrc",opened["native"]["keyEvents"]>s["native"]["keyEvents"] and opened["mc"]["screen"].endswith(".InventoryScreen"))
        slots=opened["mc"]["menuSlots"]
        key(101)
        e=wait(lambda s:s["mc"]["uiAccepted"]>opened["mc"]["uiAccepted"],"E delivered to open inventory")
        check("E no longer closes the inventory",e["mc"]["screen"]==opened["mc"]["screen"] and e["native"]["uiActive"])
        key(105);closed=screen(False)
        check("I closes the same inventory",not closed["native"]["hostGameMenu"])
        key(105);opened=screen(True)
        consumed=opened["native"]["hostUiConsumed"]
        key(27);closed=screen(False)
        report["phases"]["escape"]=closed
        check("Escape closes MC inventory before CS GameUI consumes it",closed["native"]["hostUiConsumed"]>consumed and not closed["native"]["hostGameMenu"])

        messages=closed["native"]["hostUseMessages"]
        key(101)
        used=wait(lambda s:s["native"]["hostUseMessages"]>messages,"E native Use forwarding")
        check("E still reaches the native Use path during MC gameplay",not used["native"]["uiActive"])
        report["phases"]["use"]=used

        response=game.minecraft(f"execute in {DIM} if block 12 65 -6 minecraft:air")
        if "passed" not in response.lower():raise RuntimeError("Pick-item fixture block position is occupied")
        slot=next((v["id"]-36 for v in slots if 36<=v["id"]<=44 and v["count"]==0),None)
        if slot is None:raise RuntimeError("No empty hotbar slot for reversible creative pick fixture")
        if any(v["item"]=="minecraft:gold_block" and v["count"] for v in slots):
            raise RuntimeError("Pick-item fixture must start without an existing gold block")
        game.minecraft(f"execute in {DIM} run setblock 12 65 -6 minecraft:gold_block");created_block=True
        game.minecraft(f"execute in {DIM} run tp {game.player} 10.0 64.001 -5.5")
        game.host("view 0 0")
        wait(lambda s:abs(s["mc"]["position"][0]-10)<.02 and abs(s["mc"]["position"][2]+5.5)<.02,"pick-item camera fixture")
        game.minecraft(f"gamemode creative {game.player}");time.sleep(.4)
        key(49+slot);wait(lambda s:s["mc"]["selectedSlot"]==slot and s["mc"]["selectedCount"]==0,"empty pick slot")
        created_slot=slot
        key(243)
        picked=wait(lambda s:s["mc"]["selectedItem"]=="minecraft:gold_block" and s["mc"]["selectedCount"]==1,"creative MOUSE3 copy")
        report["phases"]["creative_pick"]={"state":picked,"capture":capture("middle-pick")}
        check("MOUSE3 uses vanilla creative pick-block",picked["mc"]["selectedSlot"]==slot)
        game.minecraft(f"gamemode survival {game.player}")
        other=(slot+1)%9;key(49+other)
        wait(lambda s:s["mc"]["selectedSlot"]==other,"another hotbar slot")
        key(243)
        picked=wait(lambda s:s["mc"]["selectedSlot"]==slot,"survival MOUSE3 selection")
        check("MOUSE3 selects existing hotbar item in survival",picked["mc"]["selectedItem"]=="minecraft:gold_block" and picked["mc"]["selectedCount"]==1)
        report["phases"]["survival_pick"]=picked
        check("B menu and slot inputs leave A unchanged",all(picked["a"][k]==report["before"]["a"][k] for k in ("minecraftMenu","hudMenuId","hudSlot")))
        check("Renderer remains valid",picked["native"]["glError"]==0)
        report["outcome"]="passed"
    except Exception as error:
        report["outcome"]="incomplete";report["error"]=str(error);report["atFailure"]=snapshot()
    finally:
        try:
            game.host("menu_close")
            if created_slot is not None:game.minecraft(f"item replace entity {game.player} hotbar.{created_slot} with minecraft:air")
            if created_block:game.minecraft(f"execute in {DIM} if block 12 65 -6 minecraft:gold_block run setblock 12 65 -6 minecraft:air")
            if original:
                game.minecraft(f"gamemode {['survival','creative','adventure','spectator'][original['mode']]} {game.player}")
                game.minecraft(f"execute in {DIM} run tp {game.player} {' '.join(map(str,original['position']))}")
                game.host(f"slot {original['slot']+1}");game.host(f"view {original['view'][1]} {original['view'][0]}")
        except Exception as error:report["cleanupError"]=str(error)
        game.connection.close()
        prefix.with_suffix(".json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps({"report":str(prefix.with_suffix('.json')),"outcome":report["outcome"],"checks":report["checks"],"error":report.get("error"),"cleanupError":report.get("cleanupError")},indent=2))
    return 0 if report["outcome"]=="passed" and "cleanupError" not in report else 1


if __name__=="__main__":raise SystemExit(main())
