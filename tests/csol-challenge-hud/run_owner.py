#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the actual phase UI consumer against matched SDK interfaces and real GDI."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
ROOT=Path(__file__).resolve().parents[2]
def identity(path):
    path=Path(path).resolve(strict=True);data=path.read_bytes()
    return dict(path=str(path),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
def split(command):
    count=ctypes.c_int();shell=ctypes.windll.shell32
    shell.CommandLineToArgvW.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_int)]
    shell.CommandLineToArgvW.restype=ctypes.POINTER(ctypes.c_wchar_p)
    args=shell.CommandLineToArgvW(command,ctypes.byref(count))
    if not args:raise ctypes.WinError()
    try:return[args[i]for i in range(count.value)]
    finally:
        release=ctypes.windll.kernel32.LocalFree;release.argtypes=[ctypes.c_void_p];release(args)
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in('client-build','environment','font','vgui-library','output'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve();build=args.client_build.resolve(strict=True)
    if out.exists()or not out.is_relative_to(ROOT/'build'):raise ValueError('Use a fresh workspace output directory')
    env=os.environ|json.loads(args.environment.read_text('utf-8-sig'))
    cache=(build/'CMakeCache.txt').read_text('utf-8-sig');ninja=re.search(r'^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$',cache,re.M)[1].strip()
    database=json.loads(subprocess.check_output([ninja,'-C',str(build),'-t','compdb'],env=env))
    row=next(r for r in database if r['file'].replace('\\','/').endswith('/client/scenario/core/challenge_phase.cpp'))
    original=split(row['command']);flags=[];skip=False
    for value in original[1:]:
        if skip:skip=False;continue
        if value.lower()in('-o','-mf','-mt'):skip=True;continue
        if value in('/c','-c','-MD','--')or value.lower().endswith('.cpp')or value.lower().startswith(('/fo','/fd')):continue
        flags.append(value)
    flags+=['/I'+str(ROOT),'/DCSOL_RESOURCE_REFRESH=1','/showIncludes']
    sources=[ROOT/'tests/resource-refresh/probe_phase.cpp',ROOT/'native/csol/client/resource_font_memory.cpp']
    paths=[*sources,ROOT/'native/csol/client/scenario/core/challenge_phase.cpp',ROOT/'native/csol/client/scenario/core/challenge_phase.hpp',ROOT/'native/csol/scenario/custom/phase_snapshot.hpp',ROOT/'native/csol/scenario/custom/phase_art.hpp',ROOT/'tests/csol-challenge-hud/ui_fixture.hpp',ROOT/'tests/csol-hud-style/allocation_probe.hpp',Path(__file__),args.environment,args.font,args.vgui_library,Path(original[0]),build/'build.ninja',build/'CMakeCache.txt']
    before=[identity(p)for p in paths];out.mkdir(parents=True);steps=[];dependencies={};success=False
    def run(name,command):
        p=subprocess.run(list(map(str,command)),cwd=build,env=env,capture_output=True,timeout=180)
        data=p.stdout+p.stderr;(out/(name+'.log')).write_bytes(data);text=data.decode('utf8','replace')
        for path in re.findall(r'Note: including file:\s*([^\r\n]+)',text):
            path=Path(path.strip()).resolve(strict=True);dependencies[str(path)]=identity(path)
        steps.append(dict(name=name,command=list(map(str,command)),exit=p.returncode));print(name,p.returncode,flush=True)
        if p.returncode:print(text[-6000:]);raise RuntimeError(name)
        return text
    try:
        objects=[]
        for i,source in enumerate(sources):
            obj=out/f'{i}.obj';objects.append(obj);run('compile-'+str(i),[original[0],*flags,'/c','/Fo'+str(obj),source])
        exe=out/'owner.exe'
        run('link',[original[0],'/nologo','--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',*objects,args.vgui_library.resolve(),'/Fe'+str(exe),'/link','/machine:x86','/threads:1','/OPT:REF','gdi32.lib','user32.lib','imm32.lib','winmm.lib','advapi32.lib'])
        text=run('test',[exe,args.font.resolve(),args.font.resolve()]);match=re.search(r'PASS (\d+) actual-owner checks',text)
        if not match:raise RuntimeError('missing actual UI summary')
        success=True
    finally:
        stable=before==[identity(p)for p in paths]and all(identity(p)==r for p,r in dependencies.items())
        result=dict(success=success and stable,inputsUnchanged=stable,checks=int(match[1])if success else 0,steps=steps,inputs=before,dependencies=list(dependencies.values()),deployed=False,gameplay=False,boundary='Actual phase UI/GDI owner with controlled engine and VGUI surface; no live GPU or gameplay performance')
        (out/'receipt.json').write_text(json.dumps(result,indent=2),'utf8')
    if not result['success']:raise SystemExit(1)
    print(json.dumps(dict(success=True,checks=result['checks'],inputsUnchanged=stable)))
if __name__=='__main__':main()
