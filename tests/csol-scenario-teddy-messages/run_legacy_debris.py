#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the former extra coordinate guard with the exact prior method body.

Only one method is extracted into a generated test adapter; project sources
are neither copied into a candidate tree nor modified by this runner.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]
REVISION='aeb10d64'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--build-log',required=True,type=Path)
    parser.add_argument('--environment',required=True,type=Path)
    args=parser.parse_args()
    spec=importlib.util.spec_from_file_location('message_sdk',Path(__file__).with_name('run_current_sdk.py'))
    sdk=importlib.util.module_from_spec(spec);spec.loader.exec_module(sdk)
    out=sdk.no_links(args.output).resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():
        raise ValueError('Fresh build output required')
    revision=subprocess.check_output(['git','rev-parse',REVISION],cwd=ROOT,text=True).strip()
    source=subprocess.check_output(['git','show',revision+':native/csol/server/scenario/core/scenario_messages.cpp'],cwd=ROOT).decode('utf8')
    start=source.index('bool CCSOLScenarioMessages::SendTeddyTentacleDebris(')
    opening=source.index('{',start);depth=1;end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    method=source[start:end]
    assert 'origin[axis] < -4096.0f' in method and 'origin[axis] > 4095.875f' in method
    out.mkdir(parents=True)
    adapter=out/'previous-debris-adapter.cpp'
    adapted=method.replace('CCSOLScenarioMessages::SendTeddyTentacleDebris','LegacyDebris',1).replace(') const\n{',')\n{',1)
    adapter.write_text('#include "precompiled.h"\n#include <cmath>\n'
        'namespace { bool g_scenarioMessageOpen{}; struct ScenarioMessageScope { }; }\n'+adapted+'\n','utf8')
    command,flags=sdk.compiler_flags(args.build_log)
    flags=[token for token in flags if token.lower()!='/c']
    env=dict(os.environ)
    for key,value in sdk.read(args.environment).items():
        for existing in list(env):
            if existing.casefold()==key.casefold():del env[existing]
        env[key]=value
    fixture=Path(__file__).with_name('legacy_debris.cpp')
    before=[sdk.identity(path) for path in (fixture,Path(__file__),args.build_log,args.environment,adapter)]
    exe=out/'legacy-debris.exe'
    job=[command[0],*flags,'/clang:-fuse-ld=lld',str(adapter),str(fixture),'/Fe'+str(exe),'/link','/threads:1','/opt:lldltojobs=1']
    rsp=out/'compile.rsp';rsp.write_text(subprocess.list2cmdline(job[1:]),'utf8')
    done=subprocess.run([command[0],'@'+str(rsp)],cwd=out,env=env,capture_output=True,timeout=180)
    (out/'compile.log').write_bytes(done.stdout+done.stderr)
    if done.returncode:raise RuntimeError('Legacy SDK compile failed; see compile.log')
    observed=subprocess.run([str(exe)],cwd=ROOT,capture_output=True,timeout=20)
    (out/'run.log').write_bytes(observed.stdout+observed.stderr)
    success=observed.returncode==1 and b'legacy extra signed-short guard' in observed.stderr
    unchanged=all(sdk.identity(row['path'])==row for row in before)
    receipt=dict(success=success and unchanged,inputsUnchanged=unchanged,revision=revision,
        methodSha256=hashlib.sha256(method.encode()).hexdigest(),sourceCopies=0,
        generatedAdapter=sdk.identity(adapter),compileExit=done.returncode,
        expectedFailureExit=1,observedExit=observed.returncode,inputs=before,
        boundary='Exact prior sender method plus controlled SDK message callbacks; no game DLL or runtime deployment.')
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf8')
    print(json.dumps(dict(success=receipt['success'],observedExit=observed.returncode,revision=revision)))
    return 0 if receipt['success'] else 1

if __name__=='__main__':raise SystemExit(main())
