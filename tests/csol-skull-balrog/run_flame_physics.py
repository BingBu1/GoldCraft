#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run original ReHLDS physics bodies against actual recursive box hull traces."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT=Path(__file__).resolve().parents[2]

def identity(path):
    path=Path(path).resolve()
    with path.open('rb') as stream: digest=hashlib.file_digest(stream,'sha256').hexdigest()
    return dict(path=str(path),bytes=path.stat().st_size,sha256=digest)

def read(path):return json.loads(Path(path).read_text('utf-8-sig'))

def split_windows(command):
    function=ctypes.windll.shell32.CommandLineToArgvW
    function.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_int)]
    function.restype=ctypes.POINTER(ctypes.c_wchar_p)
    count=ctypes.c_int()
    arguments=function('tool '+command,ctypes.byref(count))
    if not arguments:raise ctypes.WinError()
    try:return [arguments[index] for index in range(1,count.value)]
    finally:
        ctypes.windll.kernel32.LocalFree.argtypes=[ctypes.c_void_p]
        ctypes.windll.kernel32.LocalFree(arguments)

def build_commands(meta,out):
    flags=[]
    for arg in split_windows(meta['originalCompileCommand']):
        lower=arg.lower()
        if lower.startswith(('/fo','/fd','/yu','/yc')) or lower.endswith(('.cpp','.cc')):continue
        if lower.startswith('/fp') and lower!='/fp:precise':continue
        flags.append(arg)
    compiler=Path(meta['compiler']['path']);linker=compiler.with_name('lld-link.exe')
    compile=[str(compiler),'/I'+str(out),*flags,'/Y-','/Fo'+str(out/'physics.obj'),'/Fd'+str(out/'compile.pdb'),str(out/'main.cpp')]
    memorylib=Path(next(arg for arg in meta['finalLinkCommand']['argv'] if arg.lower().endswith('/libacof32.lib')))
    link=[str(linker),'/nologo','/machine:X86','/subsystem:console','/OPT:REF','/OPT:ICF','/OPT:LLDLTO=3','/threads:1','/opt:lldltojobs=1',
        '/out:'+str(out/'physics.exe'),str(out/'physics.obj'),str(memorylib)]
    return compile,link,memorylib

def body(path,signature,occurrence=0):
    text=path.read_text('utf-8-sig');start=-1
    for _ in range(occurrence+1):start=text.index(signature,start+1)
    opening=text.index('{',start);depth=0
    for end in range(opening,len(text)):
        depth+=(text[end]=='{')-(text[end]=='}')
        if depth==0:return text[start:end+1]+'\n'
    raise ValueError(signature)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--baseline',type=Path,required=True,help='JSON with the recorded x86 engine compile/link commands and environment path')
    args=parser.parse_args();out=args.output.resolve()
    assert out.is_relative_to(ROOT/'build') and not out.exists()
    for path in (out,*out.parents):assert not path.is_symlink() and not path.is_junction()
    out.mkdir(parents=True);(out/'temp').mkdir()
    inputs=[];slices=[]
    fixture=Path(__file__).with_name('flame_physics.cpp')
    for path in (fixture,Path(__file__),args.baseline):inputs.append(identity(path))
    (out/'main.cpp').write_bytes(fixture.read_bytes())
    engine=ROOT/'external/ReHLDS/rehlds/engine'
    selections=[('mathlib.cpp','real_t _DotProduct(',0),('mathlib.cpp','void VectorMA(',0),
        ('world.cpp','void SV_InitBoxHull(',0),('world.cpp','hull_t *SV_HullForBox(',0),
        ('world.cpp','int SV_HullPointContents(',0),('world.cpp','qboolean SV_RecursiveHullCheck(',1),
        ('pr_cmds.cpp','void EXT_FUNC SV_SetGlobalTrace(',0),
        ('sv_phys.cpp','qboolean SV_RunThink(',0),('sv_phys.cpp','void SV_Impact(',0),
        ('sv_phys.cpp','void SV_ClipVelocity(',0),('sv_phys.cpp','trace_t SV_PushEntity(',0),
        ('sv_phys.cpp','void SV_Physics_Bounce(',0)]
    chunks=[]
    for filename,signature,occurrence in selections:
        source=engine/filename;value=body(source,signature,occurrence);chunks.append(value)
        slices.append(dict(source=identity(source),signature=signature,occurrence=occurrence,
            extractedSha256=hashlib.sha256(value.encode()).hexdigest()))
    (out/'flame_physics_engine.inc').write_text('\n'.join(chunks),'utf-8')
    # Use the established x86 engine toolchain/environment and real SDK headers.
    meta=read(args.baseline)
    compile,link,memorylib=build_commands(meta,out)
    inputs.append(identity(memorylib))
    inputs.append(identity(meta['environment']['path']))
    env=os.environ.copy();env.update(read(meta['environment']['path']))
    env.update(TMP=str(out/'temp'),TEMP=str(out/'temp'),VSLANG='1033')
    for path in (out/'main.cpp',out/'flame_physics_engine.inc',compile[0],link[0]):inputs.append(identity(path))
    inputs.extend({row['source']['path']:row['source'] for row in slices}.values())
    result=dict(success=False,steps=[],inputs=inputs,extraction=slices,includes=[],
        scope='Unmodified ReHLDS Bounce/PushEntity/ClipVelocity/RunThink/Impact and recursive BSP-hull bodies; actual x86 SDK. Controlled dry, unrotated box scene and game Touch/Think callbacks. No engine process, networking, studio hitboxes, area-tree relinking or live gameplay.')
    (out/'plan.json').write_text(json.dumps(dict(commands=[compile,link,[str(out/'physics.exe')]],**result),indent=2),'utf-8')
    try:
        for label,command in [('compile',compile),('link',link),('run',[str(out/'physics.exe')])]:
            started=time.monotonic();proc=subprocess.run(command,cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
            log=out/(label+'.log');log.write_bytes(proc.stdout)
            text=proc.stdout.decode('utf-8','replace')
            if label=='compile':result['includes']=[identity(p) for p in sorted(set(re.findall(r'^\s*Note: including file:\s*(.*?)\s*$',text,re.M)),key=str.lower)]
            result['steps'].append(dict(label=label,exit=proc.returncode,seconds=time.monotonic()-started,log=identity(log)))
            print(label,proc.returncode,flush=True)
            if proc.returncode:
                print(text[-8000:]);raise RuntimeError(label+' failed')
        rows=[json.loads(line) for line in (out/'run.log').read_text().splitlines() if line.startswith('{')]
        (out/'trajectories.json').write_text(json.dumps(dict(columns=['time','x','vx','targetSolid','flameFlags'],cases=rows),indent=2),'utf-8')
        result['trajectory']=identity(out/'trajectories.json')
        result['summary']=re.search(r'\d+ checks, \d+ failures',(out/'run.log').read_text())[0]
        result['success']=True
    except Exception as error:result['error']=str(error)
    finally:
        result['inputsUnchanged']=all(identity(row['path'])==row for row in inputs)
        result['success']&=result['inputsUnchanged']
        (out/'result.json').write_text(json.dumps(result,indent=2),'utf-8')
    print(json.dumps({k:v for k,v in result.items() if k not in ('inputs','includes','extraction')},indent=2))
    if not result['success']:raise SystemExit(1)

if __name__=='__main__':main()
