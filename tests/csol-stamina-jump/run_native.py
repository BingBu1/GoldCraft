#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Recheck existing fluid/status/water PM extracts after jump integration."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]


def identity(path):return dict(path=str(path.resolve()),sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('compiler','environment','common','output'):parser.add_argument('--'+key,type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve()
    if out.exists()or not out.is_relative_to(ROOT/'build'):raise ValueError('Use a fresh workspace output directory')
    out.mkdir();env=dict(os.environ)
    for key,value in json.loads(args.environment.read_text('utf-8-sig')).items():
        for old in list(env):
            if old.casefold()==key.casefold():del env[old]
        env[key]=value
    paths=[ROOT/'tests/native'/name for name in ('fluid_movement_tests.cpp','status_motion_tests.cpp','native_water_tests.cpp','extract_fluid_movement.py','extract_status_motion.py')]
    paths += [ROOT/'external/ReGameDLL_CS/regamedll/pm_shared/pm_shared.cpp',ROOT/'native/csol/server/custom/action_stamina.h',args.compiler,args.common,args.environment,Path(__file__)]
    before=[identity(path)for path in paths];steps=[];success=False

    def run(name,command):
        done=subprocess.run(list(map(str,command)),cwd=out,env=env,capture_output=True,timeout=120)
        text=(done.stdout+done.stderr).decode('utf8','replace');(out/(name+'.log')).write_text(text,'utf8')
        steps.append(dict(name=name,command=list(map(str,command)),exit=done.returncode));print(name,done.returncode,flush=True)
        if done.returncode:print(text[-4000:]);raise RuntimeError(name)
        if name.endswith('-test'):print(text,flush=True)

    try:
        pm=ROOT/'external/ReGameDLL_CS/regamedll/pm_shared/pm_shared.cpp'
        for script,target in [('extract_fluid_movement.py','fluid_movement_actual.inc'),('extract_status_motion.py','motion_gravity_actual.inc')]:
            run(script,['python','-X','utf8','-B',ROOT/'tests/native'/script,pm,out/target])
        for name in ('fluid_movement_tests','status_motion_tests','native_water_tests'):
            obj=out/(name+'.obj');exe=out/(name+'.exe')
            flags=['/nologo','--target=i686-pc-windows-msvc','/std:c++20','/EHsc','/MT','/DNOMINMAX','/O2','/arch:AVX2','/fp:precise','/utf-8',
                   '/I'+str(out),'/I'+str(ROOT/'native/include')]
            flags += ['/I'+str(ROOT/'external/ReGameDLL_CS/regamedll'/folder)for folder in ('pm_shared','common','engine')]
            run(name+'-compile',[args.compiler,*flags,'/c',ROOT/'tests/native'/(name+'.cpp'),'/Fo'+str(obj)])
            run(name+'-link',[args.compiler,'/nologo','--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',obj,'/Fe'+str(exe),'/link','/machine:x86','/threads:1',args.common.resolve(),'bcrypt.lib','ws2_32.lib'])
            run(name+'-test',[exe])
        success=True
    finally:
        stable=before==[identity(path)for path in paths]
        (out/'receipt.json').write_text(json.dumps(dict(success=success and stable,inputsUnchanged=stable,steps=steps,inputs=before,
            boundary='Existing actual PM extracts for fluid/status/native water; stamina APIs have explicit absent-owner sinks here, real ownership is independently tested'),indent=2)+'\n','utf8')
    if not(success and stable):raise SystemExit(1)


if __name__=='__main__':main()
