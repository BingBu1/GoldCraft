#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the actual x86 CRC wrapper against immutable Git and bitwise oracles.

Outputs stay in build/engine-crc32. Source trees and game assets are not copied.
Only exact function extracts used by the single test TU are generated.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import time
import zlib

ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
OUT=ROOT/"build/engine-crc32"


def identity(path: Path) -> dict:
    path=path.resolve(strict=True);data=path.read_bytes()
    return {"path":str(path),"bytes":len(data),"sha256":hashlib.sha256(data).hexdigest()}


def write(path: Path,value: object) -> None:
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding="utf-8")


def git(*arguments: str) -> bytes:
    return subprocess.check_output(["git","-C",str(ROOT/"external/ReHLDS"),*arguments],creationflags=subprocess.CREATE_NO_WINDOW)


def extract(data: bytes,namespace: str,span) -> tuple[bytes,list]:
    marker=b"static const uint32 pulCRCTable[256] ="
    if data.count(marker)!=1:raise ValueError("CRC table is ambiguous")
    start=data.index(marker);end=data.index(b"};",start)+2
    chunks=[data[start:end]];ranges=[{"label":"table","start":start,"end":end,"sha256":hashlib.sha256(chunks[0]).hexdigest()}]
    for name in ("CRC32_Init","CRC32_Final","CRC32_ProcessByte","CRC32_ProcessBuffer"):
        signature=("CRC32_t" if name=="CRC32_Final" else "void")+" EXT_FUNC "+name+"("
        start,end=span(data,signature);chunk=data[start:end];chunks.append(chunk)
        ranges.append({"label":name,"start":start,"end":end,"sha256":hashlib.sha256(chunk).hexdigest()})
    notice=data[:data.index(b'#include "precompiled.h"')]
    return notice+f"namespace {namespace} {{\n".encode()+b"\n".join(chunks)+b"\n}\n",ranges


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-plan",type=Path,default=ROOT/"build/engine-studio-storage-tests/plan.json")
    parser.add_argument("--model-plan",type=Path,default=ROOT/"build/engine-studio-safety-bench/plan.json")
    parser.add_argument("--legacy-ref",default="550f2d62f13f4ebeb029c1d9d1c212133202611d")
    parser.add_argument("--rounds",type=int,default=3)
    parser.add_argument("--prepare-only",action="store_true")
    args=parser.parse_args()
    if not 1<=args.rounds<=8:parser.error("rounds must be 1..8")
    if OUT.resolve()!=OUT:raise ValueError("unexpected output redirection")
    OUT.mkdir(parents=True,exist_ok=True)
    helper=ROOT/"tests/engine-studio-storage/run_resources.py"
    spec=importlib.util.spec_from_file_location("crc_extract_helper",helper)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    base=json.loads(args.build_plan.read_text(encoding="utf-8-sig"))
    command=next(row["argv"] for row in base["commands"] if row["name"]=="compile-0")
    flags=command[:command.index("/c")]
    if any(flag not in flags for flag in ("--target=i686-pc-windows-msvc","/clang:-O3","/clang:-flto=thin","/arch:AVX2","/fp:precise")):
        raise ValueError("fixture build plan is not x86 O3/ThinLTO/AVX2/precise")
    flags.append(f"/I{OUT}")
    environment_path=Path(base["environmentInput"]["path"])
    revision=git("rev-parse",args.legacy_ref+"^{commit}").decode().strip()
    legacy=git("show",revision+":rehlds/engine/crc.cpp")
    legacy_blob=git("rev-parse",revision+":rehlds/engine/crc.cpp").decode().strip()
    current_path=ROOT/"external/ReHLDS/rehlds/engine/crc.cpp"
    current=current_path.read_bytes()
    legacy_code,legacy_ranges=extract(legacy,"legacy_crc",module.span)
    current_code,current_ranges=extract(current,"current_crc",module.span)
    for old,new in zip(legacy_ranges[:4],current_ranges[:4]):
        # Ignore line endings; these APIs and the original table are not part of the change.
        if legacy[old["start"]:old["end"]].replace(b"\r\n",b"\n")!=current[new["start"]:new["end"]].replace(b"\r\n",b"\n"):
            raise ValueError("CRC table/Init/Final/ProcessByte changed outside intended wrapper")
    for name,code in (("legacy-crc.inc",legacy_code),("current-crc.inc",current_code)):(OUT/name).write_bytes(code)
    model_plan=json.loads(args.model_plan.read_text(encoding="utf-8"))
    models=[Path(row["path"]).resolve(strict=True) for row in model_plan["models"]]
    if not models or len(set(models))!=len(models) or any(not path.is_relative_to(ROOT/"sandbox") for path in models):
        raise ValueError("invalid or repeated model set")
    model_list=OUT/"models.txt";model_list.write_text("\n".join(map(str,models))+"\n",encoding="utf-8")
    fixture=HERE/"fixture.cpp";obj=OUT/"test.obj";exe=OUT/"test.exe"
    commands=[
        {"name":"compile","argv":[*flags,"/c",str(fixture),f"/Fo{obj}"]},
        {"name":"link","argv":[flags[0],"--target=i686-pc-windows-msvc","-fuse-ld=lld","/MT",str(obj),f"/Fe{exe}",
                                 "/link","/OPT:REF","/OPT:ICF","/OPT:LLDLTO=3","/threads:1"]},
        {"name":"test","argv":[str(exe),str(model_list),str(args.rounds)]},
    ]
    source_paths=[fixture,Path(__file__),helper,current_path,ROOT/"native/engine/crc32_ieee.h",
                  OUT/"legacy-crc.inc",OUT/"current-crc.inc",model_list]
    plan={"commands":commands,"sources":[identity(path) for path in source_paths],
          "legacy":{"revision":revision,"blob":legacy_blob,"sha256":hashlib.sha256(legacy).hexdigest(),"ranges":legacy_ranges},
          "current":{"identity":identity(current_path),"ranges":current_ranges},"buildPlan":identity(args.build_plan),
          "modelPlan":identity(args.model_plan),"environment":identity(environment_path),"compiler":identity(Path(flags[0])),
          "models":list(map(str,models)),"prepareOnly":args.prepare_only,"sourceTreesCopied":False,"liveGame":False}
    write(OUT/"plan.json",plan)
    if args.prepare_only:
        print(json.dumps({"prepared":str(OUT/"plan.json"),"models":len(models),"compiled":False}));return 0
    result={"success":False,"commands":[],"liveGame":False};began=time.monotonic()
    try:
        seed_values=(0,1,0xffffffff,0x12345678,0x80000000,0xdeadbeef,0x55555555,0xaaaaaaaa)
        inputs=[]
        for path in models:
            data=path.read_bytes()
            inputs.append({"path":str(path),"bytes":len(data),"sha256":hashlib.sha256(data).hexdigest(),
                           "rawCrcByRound":[(zlib.crc32(data,seed_values[i]^0xffffffff)^0xffffffff)&0xffffffff for i in range(args.rounds)]})
        result["models"]=inputs
        environment=dict(os.environ);overrides=json.loads(environment_path.read_text(encoding="utf-8-sig"))
        if not isinstance(overrides,dict) or not all(isinstance(k,str) and isinstance(v,str) for k,v in overrides.items()):
            raise ValueError("build environment must be a string dictionary")
        environment.update(overrides);environment["VSLANG"]="1033"
        for command in commands:
            start=time.monotonic()
            ran=subprocess.run(command["argv"],cwd=OUT,env=environment,capture_output=True,text=True,
                               timeout=180,creationflags=subprocess.CREATE_NO_WINDOW)
            log=OUT/f"{command['name']}.log";log.write_text(ran.stdout+ran.stderr,encoding="utf-8")
            result["commands"].append({"name":command["name"],"exit":ran.returncode,"seconds":time.monotonic()-start,"log":identity(log)})
            if ran.returncode:raise RuntimeError(f"{command['name']} exited {ran.returncode}; see {log}")
        headers={Path(line.split("including file:",1)[1].strip()).resolve()
                 for line in (OUT/"compile.log").read_text(encoding="utf-8").splitlines() if "including file:" in line}
        result["actualIncludes"]=[identity(path) for path in sorted(headers)]
        samples=[];observed=[];mutations=[];checks={};complete=None
        for line in (OUT/"test.log").read_text(encoding="utf-8").splitlines():
            fields=line.split("\t")
            if fields[0]=="checks":checks[fields[1]]=int(fields[2])
            elif fields[0]=="model":observed.append((int(fields[1]),int(fields[2])))
            elif fields[0]=="sample":samples.append({"index":int(fields[1]),"round":int(fields[2]),"seed":int(fields[3],16),
                "rawCrc":int(fields[4],16),"legacySeconds":float(fields[5]),"currentSeconds":float(fields[6])})
            elif fields[0]=="mutations":mutations.append((int(fields[1]),int(fields[2])))
            elif fields[0]=="complete":complete=tuple(map(int,fields[1:]))
            else:raise ValueError("unexpected test output")
        if observed!=[(i,row["bytes"]) for i,row in enumerate(inputs)] or mutations!=[(i,3) for i in range(len(inputs))]:
            raise ValueError("incomplete model/mutation coverage")
        if len(samples)!=len(models)*args.rounds or {(row["index"],row["round"]) for row in samples}!={
                (i,r) for i in range(len(models)) for r in range(args.rounds)}:
            raise ValueError("incomplete benchmark coverage")
        for row in samples:
            if row["seed"]!=seed_values[row["round"]] or row["rawCrc"]!=inputs[row["index"]]["rawCrcByRound"][row["round"]]:
                raise ValueError("real model disagrees with independent zlib oracle")
        if complete!=(len(models),args.rounds,sum(checks.values())):raise ValueError("incomplete assertion totals")
        result.update(checks=checks,totalAssertions=complete[2],samples=samples,mutatedModels=len(mutations),zlibMatches=True)
        result["totalsByRound"]=[{"round":r,**{stage:sum(row[stage] for row in samples if row["round"]==r)
            for stage in ("legacySeconds","currentSeconds")}} for r in range(args.rounds)]
        result["sourcesUnchanged"]=all(identity(Path(row["path"]))==row for row in plan["sources"])
        result["modelsUnchanged"]=all(identity(Path(row["path"]))=={key:row[key] for key in ("path","bytes","sha256")} for row in inputs)
        result["executable"]=identity(exe);pe=exe.read_bytes();offset=struct.unpack_from("<I",pe,0x3c)[0]
        result["x86"]=pe[offset:offset+4]==b"PE\0\0" and struct.unpack_from("<H",pe,offset+4)[0]==0x14c
        result["success"]=result["sourcesUnchanged"] and result["modelsUnchanged"] and result["x86"]
    except Exception as error:result["error"]=str(error)
    finally:
        result["seconds"]=time.monotonic()-began;write(OUT/"result.json",result)
    print(json.dumps({key:result[key] for key in ("success","seconds")}))
    return 0 if result["success"] else 1


if __name__=="__main__":raise SystemExit(main())
