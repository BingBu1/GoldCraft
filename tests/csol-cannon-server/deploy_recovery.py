"""Exercise the actual deployment transaction on tiny isolated file fixtures.

Never launches processes or writes real runtime paths. The stop checker is
replaced with a recording stub; path guards and transaction code stay real.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
from unittest.mock import patch

p=argparse.ArgumentParser()
p.add_argument("output",type=Path)
p.add_argument("--plan-only",action="store_true")
a=p.parse_args()
output=a.output.resolve()
output.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[2]
script=root/"build/csol-cannon-20261024/deploy.py"
initial_sha=hashlib.sha256(script.read_bytes()).hexdigest()
spec=importlib.util.spec_from_file_location("cannon_deployment_under_test",script)
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
real_copy=module.shutil.copy2
checks=[]
cases=[]

def check(value,label):
    checks.append(dict(label=label,passed=bool(value)))
    if not value:raise AssertionError(label)

def exercise(name,mode):
    folder=output/name
    sandbox=folder/"sandbox"
    staging=folder/"out"
    source=folder/"input"
    for d in (sandbox,staging,source):d.mkdir(parents=True)
    module.OUT=staging;module.SANDBOX=sandbox
    module.SERVER=sandbox/"server";module.CLIENT=sandbox/"client"
    changes=[]
    for key in ("a.dll","b.pdb","c.new","metadata.json"):
        src=source/key;dst=sandbox/key
        src.write_bytes(("new-"+key).encode())
        if key!="c.new":dst.write_bytes(("old-"+key).encode())
        changes.append(dict(source=module.fingerprint(src),target=str(dst),
                            before=module.fingerprint(dst) if dst.exists() else None,
                            backup=str(staging/"runtime-before"/key)))
    protected=sandbox/"protected.dat";protected.write_bytes(b"unchanged")
    plan=dict(changes=changes,protected=[module.fingerprint(protected)])
    (staging/"deployment-plan.json").write_text(json.dumps(plan))
    stop_calls=[]
    def stopped():
        stop_calls.append(True)
        if mode=="running":raise ValueError("fixture running process")
    quarantine=staging/"failed-deployment/c.new"
    if mode=="quarantine-conflict":
        quarantine.parent.mkdir();quarantine.write_bytes(b"earlier evidence")
    if mode=="source-changed":(source/"b.pdb").write_bytes(b"source changed after plan")
    if mode=="target-appeared":(sandbox/"c.new").write_bytes(b"unplanned target")
    def copy(src,dst,*args,**kwargs):
        src,dst=Path(src),Path(dst)
        if mode in ("apply-failure","recovery-failure","quarantine-conflict","restore-corruption") and src==source/"c.new":
            dst.write_bytes(b"partial write")
            raise OSError("fixture interrupted new payload")
        if mode=="recovery-failure" and src==staging/"runtime-before/b.pdb":
            raise OSError("fixture restore b denied")
        if mode=="restore-corruption" and src==staging/"runtime-before/b.pdb":
            dst.write_bytes(b"corrupt restoration")
            return str(dst)
        return real_copy(src,dst,*args,**kwargs)
    error=None
    with patch.object(module,"stopped",stopped),patch.object(module.shutil,"copy2",copy):
        try:module.apply_plan()
        except Exception as caught:error=repr(caught)
    check(len(stop_calls)==1,name+": stopped precondition called")
    check(protected.read_bytes()==b"unchanged",name+": protected file unchanged")
    if mode=="success":
        check(error is None,name+": completed")
        check((staging/"deployment.json").exists(),name+": success manifest")
        for row in changes:
            check(Path(row["target"]).read_bytes()==Path(row["source"]["path"]).read_bytes(),name+": payload "+Path(row["target"]).name)
            if row["before"]:check(module.fingerprint(Path(row["backup"]))["sha256"]==row["before"]["sha256"],name+": backup "+Path(row["target"]).name)
    elif mode in ("running","source-changed","target-appeared"):
        check(error is not None,name+": refused")
        check((sandbox/"a.dll").read_bytes()==b"old-a.dll" and (sandbox/"b.pdb").read_bytes()==b"old-b.pdb",name+": preflight preserves old pair")
        check(not (staging/"runtime-before").exists(),name+": no backup/write before validation")
        check(not (staging/"deployment.json").exists(),name+": no false success")
    else:
        check(error is not None,name+": original failure propagated")
        failed=json.loads((staging/"deployment-failed.json").read_text())
        recovery=failed["recovery"]
        check("fixture interrupted" in failed["failure"],name+": original error retained")
        check(len(recovery)==3,name+": every applied path considered")
        check((sandbox/"a.dll").read_bytes()==b"old-a.dll",name+": recovery continues to earliest DLL")
        check((sandbox/"metadata.json").read_bytes()==b"old-metadata.json",name+": unattempted metadata preserved")
        check(not (staging/"deployment.json").exists(),name+": no false success")
        if mode in ("recovery-failure","restore-corruption"):
            check(sum(not r["restored"] for r in recovery)==1,name+": one failed restoration recorded")
            check(recovery[1]["target"]==str(sandbox/"b.pdb") and "error" in recovery[1],name+": exact failed PDB identified")
        else:check((sandbox/"b.pdb").read_bytes()==b"old-b.pdb",name+": original PDB restored")
        if mode=="quarantine-conflict":
            check(quarantine.read_bytes()==b"earlier evidence",name+": earlier quarantine retained")
            check(not recovery[0]["restored"] and "already exists" in recovery[0]["error"],name+": conflict recorded")
            check((sandbox/"c.new").read_bytes()==b"partial write",name+": unresolved new file remains explicit")
        else:
            check(not (sandbox/"c.new").exists() and quarantine.read_bytes()==b"partial write",name+": partial new file quarantined")
            if mode=="apply-failure":check(all(r["restored"] for r in recovery),name+": complete rollback confirmed")
    cases.append(dict(name=name,mode=mode,error=error,files=[module.fingerprint(x) for x in sorted(folder.rglob("*")) if x.is_file()]))

def plan_exercise(mode):
    folder=output/mode;folder.mkdir()
    module.ROOT=folder;module.OUT=folder/"build/cannon"
    module.SANDBOX=folder/"sandbox";module.SERVER=module.SANDBOX/"cs-server/Half-Life"
    module.CLIENT=module.SANDBOX/"cs-client-b/Half-Life"
    def write(path,data):path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    baseline=[]
    for ext in ("dll","pdb"):
        for src,dst in ((module.OUT/f"client/CSOLSupport.{ext}",module.CLIENT/f"cstrike/metahook/plugins/CSOLSupport.{ext}"),
                        (module.OUT/f"server/mp.{ext}",module.SERVER/f"cstrike/dlls/mp.{ext}")):
            write(src,("new-"+src.name).encode());write(dst,("old-"+dst.name).encode());baseline.append(module.fingerprint(dst))
    for src in (module.OUT/"pawn/zp50_item_csol_weapons.amxx",folder/"amxx/zombie_plague/lang/csol_weapons.txt",folder/"native/csol/config/cannon_damage.json"):
        write(src,b"small staged data")
    protected=module.SANDBOX/"protected.dat";write(protected,b"protected")
    window=folder/"build/sypb-window-20261022/csol-cannon-window-20261024.json"
    write(window,json.dumps(dict(protected=[module.fingerprint(protected)],baseline=baseline)).encode())
    metadata=module.SANDBOX/"cs-server/rehlds-deployment.json"
    write(metadata,json.dumps(dict(files=[dict(name="cstrike/dlls/mp.dll",bytes=3,sha256="old"),dict(name="swds.dll",bytes=4,sha256="keep")],extra="preserve")).encode())
    resources=[]
    for name in ("same.spr","diff.spr","new.spr"):
        src=module.OUT/"resources/cstrike/sprites"/name;write(src,("incoming-"+name).encode())
        info=module.fingerprint(src);resources.append(dict(path="sprites/"+name,bytes=info["bytes"],sha256=info["sha256"]))
        if name!="new.spr":write(module.SERVER/"cstrike/sprites"/name,b"old-atlas" if name=="diff.spr" else src.read_bytes())
    decision=dict(path="sprites/diff.spr",incomingSha256=resources[1]["sha256"],
                  installedSha256=module.fingerprint(module.SERVER/"cstrike/sprites/diff.spr")["sha256"],reason="fixture independently reviewed shared atlas")
    rows=[decision]
    if mode=="reuse-missing":rows=[]
    if mode=="reuse-wrong-incoming":decision["incomingSha256"]="wrong"
    if mode=="reuse-wrong-installed":decision["installedSha256"]="wrong"
    if mode=="reuse-no-reason":decision["reason"]=""
    if mode=="reuse-duplicate":rows.append(dict(decision))
    if mode=="reuse-unused":rows.append(dict(path="sprites/not-needed.spr",incomingSha256="x",installedSha256="y",reason="stale"))
    if mode=="staged-wrong-bytes":resources[0]["bytes"]+=1
    (module.OUT/"resources-manifest.json").write_text(json.dumps(dict(files=resources)))
    (module.OUT/"resource-reuse.json").write_text(json.dumps(rows))
    error=None
    try:module.create_plan()
    except Exception as caught:error=repr(caught)
    accepted=mode in ("reuse-valid","reuse-prechanged","reuse-postchanged")
    check((error is None)==accepted,mode+": plan acceptance")
    planpath=module.OUT/"deployment-plan.json"
    if not accepted:
        check(not planpath.exists(),mode+": no unusable plan recorded")
    else:
        plan=json.loads(planpath.read_text())
        check(len(plan["unchangedResources"])==2,mode+": same and reviewed atlases pinned")
        check(plan["resourceReuse"]==rows,mode+": explicit decision retained")
        check(len(plan["changes"])==9,mode+": only new resource and expected product files")
        check(not any(str(module.CLIENT/"cstrike/sprites") in row["target"] for row in plan["changes"]),mode+": resource server only")
        updated=json.loads((module.OUT/"rehlds-deployment-updated.json").read_text())
        check(updated["extra"]=="preserve" and updated["files"][1]["sha256"]=="keep",mode+": unrelated metadata preserved")
        check(updated["files"][0]["sha256"]==module.fingerprint(module.OUT/"server/mp.dll")["sha256"],mode+": GameDLL metadata follows candidate")
        atlas=module.SERVER/"cstrike/sprites/diff.spr"
        if mode=="reuse-prechanged":atlas.write_bytes(b"changed before apply")
        def injected_copy(src,dst,*args,**kwargs):
            result=real_copy(src,dst,*args,**kwargs)
            if mode=="reuse-postchanged" and Path(src)==module.OUT/"rehlds-deployment-updated.json":
                atlas.write_bytes(b"concurrent atlas change")
            return result
        apply_error=None
        with patch.object(module,"stopped",lambda:None),patch.object(module.shutil,"copy2",injected_copy):
            try:module.apply_plan()
            except Exception as caught:apply_error=repr(caught)
        if mode=="reuse-valid":
            check(apply_error is None and atlas.read_bytes()==b"old-atlas",mode+": reviewed atlas preserved through update")
        else:
            check(apply_error is not None,mode+": atlas change detected")
            check(not (module.OUT/"deployment.json").exists(),mode+": no false deployment success")
            check(all(Path(row["path"]).read_bytes()==("old-"+Path(row["path"]).name).encode() for row in baseline),mode+": old DLL/PDB pairs intact")
            if mode=="reuse-postchanged":
                failed=json.loads((module.OUT/"deployment-failed.json").read_text())
                check(len(failed["recovery"])==9 and all(row["restored"] for row in failed["recovery"]),mode+": all own changes rolled back after postcheck")
    cases.append(dict(name=mode,mode=mode,error=error,files=[module.fingerprint(x) for x in sorted(folder.rglob("*")) if x.is_file()]))

if not a.plan_only:
    for mode in ("success","running","source-changed","target-appeared","apply-failure","recovery-failure","quarantine-conflict","restore-corruption"):
        exercise(mode,mode)
else:
    for mode in ("reuse-valid","reuse-missing","reuse-wrong-incoming","reuse-wrong-installed","reuse-no-reason","reuse-duplicate","reuse-unused","staged-wrong-bytes","reuse-prechanged","reuse-postchanged"):
        plan_exercise(mode)
check(hashlib.sha256(script.read_bytes()).hexdigest()==initial_sha,"deployment source unchanged during fixture")
result=dict(deployment_script=str(script),deployment_script_sha256=initial_sha,scope="Tiny isolated files, real transaction, simulated stopped check; no game process or production writes",checks=checks,cases=cases)
(output/"result.json").write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding="utf-8")
print(f"PASS deployment transaction {len(checks)} checks in {len(cases)} scenarios; no production writes")
