"""Run the actual temporary-fixture installer on tiny isolated file trees."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
from unittest.mock import patch

parser=argparse.ArgumentParser()
parser.add_argument("output",type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
script=root/"build/csol-cannon-20261024/live-fixture.py"
output=args.output.resolve()
if not output.is_relative_to(root/"build/csol-cannon-20261024/server-tests"):
    raise ValueError("Fixture output must stay within the cannon test tree")
output.mkdir(parents=True,exist_ok=False)
source=script.read_bytes()
(output/"script-under-test.py").write_bytes(source)
spec=importlib.util.spec_from_file_location("temporary_cannon_fixture",script)
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
checks=[]
cases=[]

def check(condition,label):
    checks.append(dict(label=label,passed=bool(condition)))

for mode in ("success","existing-target","partial-copy","interrupted-retire","changed-payload"):
    base=output/mode;out=base/"out";server=base/"sandbox/server"
    base.mkdir();out.mkdir();server.mkdir(parents=True)
    module.OUT=out;module.RECORD=out/"live-fixture-install.json"
    module.deploy.OUT=out;module.deploy.SERVER=server;module.deploy.SANDBOX=base/"sandbox"
    module.BASE=server/"cstrike/addons/amxmodx"
    src=out/"pawn/csol_cannon_live_test.amxx";src.parent.mkdir();src.write_bytes(b"test bytecode")
    binary=module.BASE/"plugins/csol_cannon_live_test.amxx"
    config=module.BASE/"configs/plugins-cannon-test.ini"
    stop_calls=[];errors=[]
    original_copy=module.shutil.copy2;original_move=module.shutil.move
    def copying(a,b,*pos,**kw):
        if mode=="partial-copy" and Path(b)==config:
            Path(b).write_bytes(b"partial temporary config")
            raise OSError("injected copy interruption")
        return original_copy(a,b,*pos,**kw)
    if mode=="existing-target":
        binary.parent.mkdir(parents=True);binary.write_bytes(b"existing user content")
    with patch.object(module.deploy,"stopped",lambda:stop_calls.append(True)),patch.object(module.shutil,"copy2",copying):
        try:module.install()
        except Exception as e:errors.append(repr(e))
    check(len(stop_calls)==1,mode+": requires stopped runtime")
    if mode=="existing-target":
        check(bool(errors),mode+": refuses conflict")
        check(binary.read_bytes()==b"existing user content",mode+": existing payload preserved")
        check(not (out/"plugins-cannon-test.ini").exists(),mode+": no orphan stage blocks retry")
        check(not module.RECORD.exists(),mode+": no install record before preflight")
    else:
        check(module.RECORD.exists(),mode+": durable install record")
        if mode=="partial-copy":check(bool(errors),mode+": partial install reports failure")
        else:check(not errors,mode+": installed")
        if mode=="changed-payload":binary.write_bytes(b"later user change")
        first=[True]
        def moving(a,b,*pos,**kw):
            if mode=="interrupted-retire" and Path(a)==binary and first[0]:
                first[0]=False
                raise OSError("injected second retirement interruption")
            return original_move(a,b,*pos,**kw)
        with patch.object(module.shutil,"move",moving):
            try:module.retire()
            except Exception as e:errors.append(repr(e))
        if mode=="interrupted-retire":
            check(not config.exists(),mode+": unload list retired first")
            try:module.retire()
            except Exception as e:errors.append(repr(e))
        if mode=="changed-payload":
            check(binary.read_bytes()==b"later user change",mode+": refuses changed payload")
            check(json.loads(module.RECORD.read_text())["status"]!="retired",mode+": not falsely retired")
        else:
            record=json.loads(module.RECORD.read_text())
            check(not binary.exists() and not config.exists(),mode+": all temporary runtime paths retired")
            check(record["status"]=="retired",mode+": durable retirement state")
            check(len(record.get("retiredFiles",[]))==2,mode+": both retirement artifacts recorded")
            saved=list((out/"live-retired").rglob("*"))
            check(sum(p.is_file() for p in saved)==2,mode+": both payloads preserved")
    cases.append(dict(mode=mode,errors=errors,files=[module.deploy.fingerprint(p) for p in sorted(base.rglob("*")) if p.is_file()]))

result=dict(scriptSha256=hashlib.sha256(source).hexdigest(),scriptUnchanged=script.read_bytes()==source,
            checks=checks,cases=cases,total=len(checks),failed=sum(not x["passed"] for x in checks),
            boundary="Only tiny filesystem fixtures; no real runtime writes or process operations")
(output/"result.json").write_text(json.dumps(result,indent=2),encoding="utf-8")
print(f"Temporary fixture recovery: {result['total']} checks; {result['failed']} failures")
for row in checks:
    if not row["passed"]:print("FAIL "+row["label"])
raise SystemExit(bool(result["failed"]))
