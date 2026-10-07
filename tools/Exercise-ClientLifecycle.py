"""Check real Minecraft menu/reload and native CS reconnect through sandbox commands."""
import argparse
import importlib.util
import json
import time
from pathlib import Path

spec=importlib.util.spec_from_file_location("renderer_test",Path(__file__).with_name("Exercise-Renderer.py"))
renderer=importlib.util.module_from_spec(spec);spec.loader.exec_module(renderer)
ROOT=renderer.ROOT


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode",choices=("menu","resource","reconnect","all"),default="all")
    args=parser.parse_args();b=renderer.Sandbox("cs-client-b");a=renderer.Sandbox("cs-client-a")
    prefix=ROOT/"analysis/goldcraft-tests"/f"client-lifecycle-{int(time.time())}"
    report={"commands":b.commands,"checks":{},"mode":args.mode};disconnected=False

    def server():return renderer.read_json(renderer.SERVER_STATUS)
    def snapshot():
        return {"A":a.status(),"B":b.status(),"minecraftB":renderer.read_json(b.logs/"minecraft-client-status.json"),"server":server()}
    def wait_for(predicate,seconds=15):
        deadline=time.monotonic()+seconds;last=None
        while time.monotonic()<deadline:
            last=snapshot()
            if predicate(last):return last
            time.sleep(.1)
        report["timeoutState"]=last
        raise TimeoutError("The requested client lifecycle transition did not complete")

    try:
        report["before"]=snapshot()
        if not(report["before"]["A"]["minecraftControl"] and report["before"]["B"]["minecraftControl"]):
            raise RuntimeError("Both real pairs must be active")
        if args.mode in ("all","menu"):
            b.host("inventory")
            opened=wait_for(lambda s:s["B"]["minecraftMenu"] and s["minecraftB"]["screen"]!="")
            b.host("menu_close")
            closed=wait_for(lambda s:not s["B"]["minecraftMenu"] and s["minecraftB"]["screen"]=="")
            report["menu"]={"opened":opened,"closed":closed}
            report["checks"]["real_inventory_opened_and_closed_from_CS"]=True
            report["checks"]["A_not_put_in_menu"]=not opened["A"]["minecraftMenu"] and not closed["A"]["minecraftMenu"]
        if args.mode in ("all","resource"):
            before=snapshot();began=time.monotonic();b.host("resource_reload")
            after=wait_for(lambda s:s["B"]["atlasGeneration"]>before["B"]["atlasGeneration"]
                           and s["B"]["atlasAnimationPackets"]>before["B"]["atlasAnimationPackets"]+12
                           and s["B"]["vertices"]>0 and s["B"]["minecraftControl"] and s["minecraftB"]["overlay"]=="",30)
            report["resourceReload"]={"before":before,"after":after,"seconds":time.monotonic()-began,
                                      "capture":b.capture(prefix.with_name(prefix.name+"-resource.bmp"))}
            report["checks"]["resource_atlas_and_animation_recovered"]=after["B"]["glError"]==0
            report["checks"]["A_atlas_unchanged_by_B_reload"]=after["A"]["atlasUploads"]==before["A"]["atlasUploads"] and after["A"]["minecraftControl"]
        if args.mode in ("all","reconnect"):
            before=snapshot();slot=before["B"]["playerSlot"];old_actor=next(p for p in before["server"]["actors"] if p["slot"]==slot)
            disconnected=True;b.host("disconnect")
            gone=wait_for(lambda s:not any(p["slot"]==slot for p in s["server"]["actors"]))
            began=time.monotonic();b.host("reconnect")
            after=wait_for(lambda s:s["B"]["minecraftControl"] and s["B"]["vertices"]>0
                           and any(p["uuid"]==old_actor["uuid"] and p["controlled"] and p["userid"]!=old_actor["userid"] for p in s["server"]["actors"]),25)
            disconnected=False
            new_actor=next(p for p in after["server"]["actors"] if p["uuid"]==old_actor["uuid"])
            report["reconnect"]={"before":before,"disconnected":gone,"after":after,"seconds":time.monotonic()-began}
            report["checks"]["native_identity_rebound"]=new_actor["userid"]!=old_actor["userid"] and (new_actor["slot"]!=slot or new_actor["serial"]!=old_actor["serial"])
            report["checks"]["same_map_and_uuid_after_reconnect"]=before["B"]["world"]==after["B"]["world"] and old_actor["uuid"]==new_actor["uuid"]
            report["checks"]["A_remained_controlled"]=gone["A"]["minecraftControl"] and after["A"]["minecraftControl"] and before["A"]["playerSerial"]==after["A"]["playerSerial"]
        report["after"]=snapshot()
        report["outcome"]="passed" if all(report["checks"].values()) else "failed"
    except Exception as error:
        report["error"]=str(error);report["outcome"]="incomplete"
    finally:
        if disconnected:
            try:b.host("reconnect")
            except Exception as error:report["cleanupError"]=str(error)
        for sandbox in (a,b):sandbox.connection.close()
        prefix.with_suffix(".json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps({"report":str(prefix.with_suffix('.json')),"outcome":report["outcome"],"checks":report["checks"],"error":report.get("error")},indent=2))


if __name__=="__main__":main()
