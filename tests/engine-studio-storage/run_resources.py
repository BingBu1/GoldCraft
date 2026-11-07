#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the actual resource pass with current project sources, without source snapshots.

The generated translation unit is piped to clang; build holds only replaceable
compiler outputs and receipts. Run in an explicitly coordinated build window.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent

def identity(path):
    path=Path(path).resolve(strict=True); data=path.read_bytes()
    return dict(path=str(path),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())

def span(data,marker):
    marker=marker.encode()
    if data.count(marker)!=1:raise ValueError(f"nonunique definition: {marker!r}")
    start=data.index(marker); at=data.index(b"{",start); depth=0; state="code"
    while at<len(data):
        one,pair=data[at:at+1],data[at:at+2]
        if state=="line":
            if one==b"\n":state="code"
        elif state=="comment":
            if pair==b"*/":state="code"; at+=1
        elif state in ('"',"'"):
            if one==b"\\":at+=1
            elif one==state.encode():state="code"
        elif pair in (b"//",b"/*"):state="line" if pair==b"//" else "comment"; at+=1
        elif one in (b'"',b"'"):state=one.decode()
        elif one==b"{":depth+=1
        elif one==b"}":
            depth-=1
            if not depth:return start,at+1
        at+=1
    raise ValueError("unterminated function")

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,default=ROOT/"build/engine-studio-resource-tests")
    parser.add_argument("--compiler",type=Path,required=True)
    parser.add_argument("--environment-json",type=Path)
    parser.add_argument("--baseline-ref",default="198789866e437ba0dc523a49526f4077edd5c9a1",
                        help="Git revision whose patch provides the original resource pass")
    parser.add_argument("--prepare-only",action="store_true")
    args=parser.parse_args()
    out=args.out.resolve()
    if not out.is_relative_to(ROOT/"build"):raise ValueError("output must be in project build")
    out.mkdir(parents=True,exist_ok=True)
    if (out/"result.json").is_file():
        prior=json.loads((out/"result.json").read_text(encoding="utf8"))
        prior_plan=json.loads((out/"plan.json").read_text(encoding="utf8"))
        with (out/"attempts.jsonl").open("a",encoding="utf8") as stream:
            stream.write(json.dumps(dict(result=prior,plan=prior_plan,
                failureLog=(out/"test.log").read_text(encoding="utf8") if (out/"test.log").is_file() else None))+"\n")
    sources={}
    def read(relative):
        path=ROOT/relative; sources[relative]=identity(path); return path.read_bytes()
    extracts=[]
    def extract(relative,marker):
        data=read(relative); begin,end=span(data,marker); code=data[begin:end]
        extracts.append(dict(source=relative,start=begin,end=end,sha256=hashlib.sha256(code).hexdigest()))
        return code
    loader="native/engine/csol_loader.cpp"
    data=read(loader); end=data.index(b"\nvoid GoldCraftCsolSequenceDelta(")
    code=data[:end]+b"\n"+data[data.index(b"void GoldCraftPrecacheCsolResources("):data.index(b"\nbool GoldCraftLoadCsolSprite(")]
    extracts.append(dict(source=loader,ranges=[[0,end],[data.index(b"void GoldCraftPrecacheCsolResources("),data.index(b"\nbool GoldCraftLoadCsolSprite(")]],sha256=hashlib.sha256(code).hexdigest()))
    code+=b"\nextern cvar_t sv_auto_precache_sounds_in_models;\n"
    svmain="external/ReHLDS/rehlds/engine/sv_main.cpp"
    for name in ("PrecacheModelTexture","PrecacheModelSeqGroups","PrecacheModelSounds","PrecacheModelSpecifiedFiles"):
        code+=b"\n"+extract(svmain,f"void {name}(")
    code+=b"\n"+extract("external/ReHLDS/rehlds/engine/model.cpp","void* EXT_FUNC Mod_Extradata(")
    # Recover the old function from the committed patch's new side. This is
    # an actual prior source segment; only its function name is changed.
    baseline_revision=subprocess.check_output(["git","rev-parse",f"{args.baseline_ref}^{{commit}}"],cwd=ROOT,text=True).strip()
    baseline_patch=subprocess.check_output(["git","show",f"{baseline_revision}:patches/rehlds-goldcraft.patch"],cwd=ROOT)
    patch=baseline_patch.splitlines()
    start=next(i for i,line in enumerate(patch) if line==b" void PrecacheModelSpecifiedFiles()")
    old=[]
    for line in patch[start:]:
        if line.startswith(b"@@"):break
        if line[:1] in (b" ",b"+"):
            old.append(line[1:])
            if line==b" }":break
    previous=b"\n".join(old)
    if b"GoldCraftStudioResourceFile" in previous:raise ValueError("committed old control now includes new optimization")
    if b"Mod_Extradata(gc_precache.models[i])" not in previous:raise ValueError("old control needs review")
    extracts.append(dict(source="patches/rehlds-goldcraft.patch",gitRevision=baseline_revision,
                         patchSha256=hashlib.sha256(baseline_patch).hexdigest(),oldControlSha256=hashlib.sha256(previous).hexdigest()))
    code+=b"\n"+previous.replace(b"void PrecacheModelSpecifiedFiles()",b"void LegacyPrecacheModelSpecifiedFiles()",1)
    fixture=read("tests/engine-studio-storage/resource_fixture.cpp")
    fixture=fixture.replace(b"// RESOURCE_PRODUCTION_EXTRACTS",code)
    compiler=args.compiler.resolve(strict=True)
    sdk=ROOT/"external/ReHLDS/rehlds"
    includes=[ROOT,ROOT/"native/engine",ROOT/"native/csol",ROOT/"native/include"]
    includes += [sdk/p for p in ("","metamod/include","public/rehlds","common","engine","public","pm_shared","rehlds","testsuite")]
    includes += [ROOT/"external/ReHLDS/dep/bzip2/include",ROOT/"external/ReHLDS/dep/cppunitlite/include",ROOT/"external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include"]
    defines=["REHLDS_API","REHLDS_FIXES","REHLDS_SSE","REHLDS_OPT_PEDANTIC","REHLDS_SELF","REHLDS_CHECKS","HAVE_OPT_STRTOOLS","USE_BREAKPAD_HANDLER","DEDICATED","SWDS","NDEBUG","NOMINMAX","_CRT_SECURE_NO_WARNINGS"]
    flags=["--target=i686-pc-windows-msvc","/nologo","/std:c++20","/EHsc","/MT","/utf-8","/O2","/clang:-O3","/clang:-flto=thin","/arch:AVX2","/Gy","/Gw","/fp:precise","/Y-","/showIncludes",*[f"/D{x}" for x in defines],*[f"/I{x}" for x in includes]]
    units=[ROOT/f"native/csol/{name}.cpp" for name in ("dependencies","model_codec","texture_model","model_json","events","model_source")]
    commands=[]; objects=[]
    for index,unit in enumerate([None,*units]):
        obj=out/f"unit-{index}.obj"; objects.append(str(obj))
        if unit:read(unit.relative_to(ROOT).as_posix())
        commands.append(dict(name=f"compile-{index}",argv=[str(compiler),*flags,"/TP","/c",str(unit) if unit else "-",f"/Fo{obj}"],stdin=index==0))
    exe=out/"resources.exe"
    commands.append(dict(name="link",argv=[str(compiler),"--target=i686-pc-windows-msvc","-fuse-ld=lld","/MT",*objects,f"/Fe{exe}","/link","/OPT:REF","/OPT:ICF","/OPT:LLDLTO=3","/threads:1"]))
    commands.append(dict(name="test",argv=[str(exe)]))
    # Include every local production source and header before starting. Actual
    # SDK/system include identities are recorded from the compiler's output.
    for directory in (ROOT/"native/engine",ROOT/"native/csol",ROOT/"native/include"):
        for path in directory.rglob("*"):
            if path.suffix in (".h",".hpp"):read(path.relative_to(ROOT).as_posix())
    for relative in ("external/ReHLDS/rehlds/engine/l_studio.cpp","native/engine/csol_sequence_guard.cpp","native/csol/sequences.cpp","tests/engine-studio-storage/run_resources.py"):
        read(relative)
    plan=dict(sources=list(sources.values()),extractions=extracts,stdinSha256=hashlib.sha256(fixture).hexdigest(),commands=commands,compiler=identity(compiler),snapshotSources=False)
    (out/"plan.json").write_text(json.dumps(plan,indent=2),encoding="utf8")
    if args.prepare_only:print("prepared; no compiler invoked"); return 0
    environment=dict(os.environ)
    if args.environment_json:environment.update(json.loads(args.environment_json.read_text(encoding="utf-8-sig")))
    environment["VSLANG"]="1033"
    result=dict(success=False,results=[],actualIncludes=[],liveGameplay=False)
    try:
        headers=set()
        for command in commands:
            begin=time.monotonic()
            run=subprocess.run(command["argv"],input=fixture if command.get("stdin") else None,cwd=ROOT,env=environment,capture_output=True,timeout=180,creationflags=subprocess.CREATE_NO_WINDOW)
            text=run.stdout+run.stderr; (out/f"{command['name']}.log").write_bytes(text)
            result["results"].append(dict(name=command["name"],exit=run.returncode,seconds=time.monotonic()-begin))
            for line in text.decode("utf8",errors="replace").splitlines():
                if "including file:" in line:headers.add(line.split("including file:",1)[1].strip())
            if run.returncode:raise RuntimeError(f"{command['name']} failed; see output log")
        result["actualIncludes"]=[identity(p) for p in sorted(headers)]
        result["success"]=True
    except Exception as error:result["error"]=str(error)
    finally:
        result["inputsUnchanged"]=all(identity(item["path"])["sha256"]==item["sha256"] for item in sources.values())
        result["success"] &= result["inputsUnchanged"]
        result["artifacts"]=[identity(path) for path in out.iterdir() if path.suffix in (".obj",".exe")]
        (out/"result.json").write_text(json.dumps(result,indent=2),encoding="utf8")
    print(json.dumps({k:result[k] for k in ("success","inputsUnchanged","results")}))
    return 0 if result["success"] else 1

if __name__=="__main__":raise SystemExit(main())
