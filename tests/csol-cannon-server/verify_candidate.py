"""Verify the frozen compatible source, then optionally inspect linked PE/PDB."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess

p=argparse.ArgumentParser()
p.add_argument("--stage",choices=("before","after"),required=True)
p.add_argument("--version",choices=(2,3,4),type=int,default=2)
a=p.parse_args()
root=Path(__file__).resolve().parents[2]
case=root/"build/csol-cannon-20261024"
out=case/"server-tests"
manifest_path=case/f"server-candidate-v{a.version}.json"
manifest=json.loads(manifest_path.read_text())
initial=json.loads((case/"server-candidate.json").read_text())
sha=lambda b:hashlib.sha256(b).hexdigest()
rows=manifest["inputs"]+manifest["preservedBuild"]+[initial["common"]]
verified=[]
for row in rows:
    path=Path(row["path"]);data=path.read_bytes()
    assert len(data)==row["bytes"] and sha(data)==row["sha256"],str(path)
    verified.append(row)
listed={str(Path(row["path"]).resolve()).lower() for row in manifest["inputs"]}
actual={str(path.resolve()).lower() for path in (case/"source").rglob("*") if path.is_file()}
assert listed==actual,"Unlisted or missing source files"
player=(case/"source/external/ReGameDLL_CS/regamedll/dlls/player.cpp").read_text()
assert "GoldCraft_AbsorbedMinecraftAttack" not in player
result=dict(stage=a.stage,source_count=len(manifest["inputs"]),all_inputs_match=True,
            common_and_preserved_build_match=True,excluded_mc_absorption=True,manifest_sha256=sha(manifest_path.read_bytes()),
            verified=verified)
methods=[]
for frozen in (out/"root-v2/inputs.json",out/"pickup-final/pickup-inputs.json"):
    for row in json.loads(frozen.read_text()):
        original=Path(row["path"])
        try:relative=original.relative_to(case/"source")
        except ValueError:relative=original.relative_to(root)
        path=case/"source"/relative
        data=path.read_bytes()
        if "signature" not in row:
            assert sha(data)==row["sha256"],"Tested header differs: "+str(path)
            continue
        text=data.decode("utf-8");start=text.index(row["signature"])
        brace=text.index("{",start);depth,end=1,brace+1
        while depth:
            depth+=(text[end]=="{")-(text[end]=="}");end+=1
        body=text[start:end]
        digest=sha(body.encode())
        normalized=body.replace("\r\n","\n")
        # Root tracked files and copied upstream baseline use different line
        # endings. Permit that exact representation difference, not token edits.
        variants={digest,sha(normalized.encode()),sha(normalized.replace("\n","\r\n").encode())}
        assert row["method_sha256"] in variants,"Tested method differs: "+str(path)+" "+row["signature"]
        methods.append(dict(path=str(path),signature=row["signature"],candidate_method_sha256=digest,
                            tested_method_sha256=row["method_sha256"],only_line_endings_differ=digest!=row["method_sha256"]))
result.update(tested_methods_match=True,tested_method_count=len(methods),tested_methods=methods)
if a.version==4:
    transaction=json.loads((out/'purchase-backup-v4/inputs.json').read_text())
    candidate=case/'source/native/csol/server/weapon_service.cpp'
    assert sha(candidate.read_bytes())==transaction['sha256'],"Tested transaction differs"
    result.update(tested_purchase_classes=transaction['classes'],tested_purchase_source_sha256=transaction['sha256'])
if a.stage=="after":
    assert (case/"server-build.exit").read_text(encoding="utf-8-sig").strip()=="0"
    llvm=Path(json.loads((root/".tools/clang-toolchain.json").read_text())["root"])/"bin"
    pe=subprocess.check_output([str(llvm/"llvm-readobj.exe"),"--file-header","--coff-debug-directory","--coff-exports",str(case/"server/mp.dll")],text=True)
    pdb=subprocess.check_output([str(llvm/"llvm-pdbutil.exe"),"dump","--summary",str(case/"server/mp.pdb")],text=True)
    (out/f"server-v{a.version}-pe.txt").write_text(pe,encoding="utf-8")
    (out/f"server-v{a.version}-pdb.txt").write_text(pdb,encoding="utf-8")
    assert "IMAGE_FILE_MACHINE_I386" in pe and "AddressSize: 32bit" in pe
    assert re.search(r"Name: weapon_cannon\s",pe),"Missing weapon entity export"
    for name in ("GiveFnptrsToDll","GetEntityAPI","GetNewDLLFunctions"):
        assert re.search(r"Name: "+re.escape(name)+r"\s",pe),name
    guid=re.search(r"PDBGUID: (\{[^}]+\})",pe)[1]
    pdbguid=re.search(r"GUID: (\{[^}]+\})",pdb)[1]
    age=int(re.search(r"PDBAge: (\d+)",pe)[1])
    assert guid==pdbguid and age==int(re.search(r"Age: (\d+)",pdb)[1])
    outputs=[]
    for path in [case/"server/mp.dll",case/"server/mp.pdb",case/"server-build.log",case/"server-build.exit",out/f"server-v{a.version}-pe.txt",out/f"server-v{a.version}-pdb.txt"]:
        data=path.read_bytes();outputs.append(dict(path=str(path),bytes=len(data),sha256=sha(data)))
    result.update(build_exit=0,machine="IMAGE_FILE_MACHINE_I386",weapon_cannon_export=True,pdb_guid=guid,pdb_age=age,outputs=outputs)
(out/f"candidate-{a.stage}{('-v'+str(a.version)) if a.version>2 else ''}.json").write_text(json.dumps(result,indent=2),encoding="utf-8")
print(f"Candidate {a.stage}: {len(manifest['inputs'])} source + common + {len(manifest['preservedBuild'])} preserved files match; no unlisted input; MC absorption excluded")
if a.stage=="after":print("x86 DLL exported weapon_cannon and server ABI; DLL/PDB identity matches "+guid)
