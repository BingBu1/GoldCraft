#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile actual stamina and action adapter; never build/deploy a full server."""
import argparse,importlib.util,json,os,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--environment',type=Path,required=True)
    p.add_argument('--msbuild-log',type=Path,required=True);p.add_argument('--sdk-seal',type=Path)
    p.add_argument('--light-provider-receipt',type=Path,
                   help='Verified real engine lightstyle service/PF writer fixture for TimeCycle')
    a=p.parse_args();spec=importlib.util.spec_from_file_location('sdk',ROOT/'tests/csol-rebuy/run_sdk.py');sdk=importlib.util.module_from_spec(spec);spec.loader.exec_module(sdk)
    out=sdk.checked(a.output,exists=False)
    if out.exists()or not out.is_relative_to(ROOT/'build'):raise ValueError('fresh workspace output required')
    env=dict(os.environ)
    for key,value in sdk.read(a.environment).items():
        for old in list(env):
            if old.casefold()==key.casefold():del env[old]
        env[key]=value
    provider=[]
    if a.sdk_seal:
        if not a.light_provider_receipt:raise ValueError('Actual TimeCycle owner requires its real lightstyle provider')
        value=sdk.read(a.light_provider_receipt)
        if not value['success'] or not value['inputsUnchanged']:raise ValueError('Engine provider build did not pass')
        provider=[value['artifact'],*value['inputs']]
        for row in provider:sdk.verify(row)
        env['GOLDCRAFT_TEST_LIGHT_PROVIDER']=value['artifact']['path']
    cmd,_=sdk.commands(a.msbuild_log)
    base=[x for x in cmd if not(x.lower().endswith(('.cpp','.c'))or x.lower().startswith(('/yu','/yc','/fo','/fd'))or(x.lower().startswith('/fp')and not x.lower().startswith('/fp:')))]
    base+=['/Y-','/showIncludes','/I'+str(ROOT/'build/scenario-sdk-current-20261108-05')]
    units={'pure':ROOT/'tests/csol-action-stamina/pure.cpp','state':ROOT/'native/csol/scenario/custom/stamina.cpp','adapter':ROOT/'native/csol/server/custom/action_stamina.cpp'}
    if a.sdk_seal:units['sdk']=ROOT/'tests/csol-action-stamina/sdk.cpp'
    tracked={};compiledHeaders=set()
    def track(path):
        row=sdk.identity(path);tracked[row['path'].casefold()]=row
    for path in [*units.values(),ROOT/'native/csol/server/custom/action_stamina.h',ROOT/'native/csol/scenario/custom/stamina.hpp',Path(__file__),a.environment,a.msbuild_log]:track(path)
    if a.light_provider_receipt:
        track(a.light_provider_receipt)
        for row in provider:track(Path(row['path']))
    out.mkdir();r=dict(success=False,sourceCopies=0,fullDLL=False,deployed=False,steps=[],inputsBefore=list(tracked.values()))
    def save():(out/'receipt.json').write_text(json.dumps(r,indent=2),'utf-8')
    def run(name,command):
        launched=command
        if name.startswith('link-'):
            response=out/(name+'.rsp');response.write_text(subprocess.list2cmdline(command[1:]),encoding='utf-8')
            launched=[command[0],'@'+str(response)]
        q=subprocess.run(launched,cwd=sdk.SDK_CWD,env=env,capture_output=True,timeout=240);text=(q.stdout+q.stderr).decode('utf-8',errors='replace');(out/(name+'.log')).write_text(text,'utf-8')
        if name.startswith('compile-'):
            for include in re.findall(r'Note: including file:\s*([^\r\n]+)',text):
                path=Path(include.strip()).resolve(strict=True)
                compiledHeaders.add(path)
                if str(path).casefold() not in tracked:track(path)
        r['steps'].append(dict(name=name,command=command,launchedCommand=launched,exit=q.returncode));save();print(name,q.returncode,text[-5000:]if q.returncode or not name.startswith('compile')else'',flush=True)
        if q.returncode:raise RuntimeError(name+' failed')
    try:
        for name,path in units.items():run('compile-'+name,[*base,'/Fo'+str(out/(name+'.obj')),str(path)])
        linker=str(Path(cmd[0]).with_name('lld-link.exe'));libs=[str(Path(d)/name)for name in('libcmt.lib','libcpmt.lib','libvcruntime.lib','libucrt.lib','oldnames.lib','kernel32.lib')for d in sdk.read(a.environment)['LIB'].split(';')if(Path(d)/name).is_file()]
        run('link-pure',[linker,'/subsystem:console','/machine:x86','/threads:1','/opt:lldltojobs=1','/out:'+str(out/'pure.exe'),str(out/'pure.obj'),str(out/'state.obj'),*libs]);run('pure',[str(out/'pure.exe')])
        if a.sdk_seal:
            seal=sdk.read(a.sdk_seal)
            for row in seal['linkInputs']:sdk.verify(row)
            sdk.verify(seal['sourceInventory'])
            baseline={row['path'].casefold():row for row in sdk.read(seal['sourceInventory']['path'])}
            owners=[units['state'],units['adapter'],ROOT/'native/csol/scenario/custom/stamina.hpp',ROOT/'native/csol/server/custom/action_stamina.h']
            # Any production header consumed by this SDK must match the source
            # that produced the sealed server objects, including class layouts.
            for path in compiledHeaders:
                if path.is_relative_to(ROOT/'native') or path.is_relative_to(ROOT/'external/ReGameDLL_CS'):
                    if path not in owners:owners.append(path)
            for path in owners:
                key=str(path.resolve()).casefold()
                if key not in baseline:raise ValueError('Full server did not inventory this production unit: '+str(path))
                sdk.verify(baseline[key])
            link=seal['linkCommand']
            remove=('/out:','/pdb:','/implib:','/def:','/ltcgout:','/manifest','/natvis:','/subsystem:','/threads:','/opt:lldltojobs=','/lldltocache:')
            job=[x for x in link if x.lower()!='/dll'and not x.lower().startswith(remove)]
            # The exact current full build already contains both production
            # objects. Append only the fixture, without duplicate definitions.
            job+=['/SUBSYSTEM:CONSOLE','/OUT:'+str(out/'owner.exe'),'/threads:1','/opt:lldltojobs=1',str(out/'sdk.obj')]
            r['actualSupportInputs']=seal['linkInputs'];r['productionOwners']=[sdk.identity(path) for path in owners]
            r['supportBoundary']='Exact current full-server objects and source-matched stamina owners; no engine process or gameplay';save()
            run('link-owner',job)
            (out/'data').mkdir();job=[str(out/'owner.exe')]
            q=subprocess.run(job,cwd=out/'data',env=env,capture_output=True,timeout=60);text=(q.stdout+q.stderr).decode('utf-8',errors='replace');(out/'owner.log').write_text(text,'utf-8');r['steps'].append(dict(name='owner',exit=q.returncode,command=job));save();print('owner',q.returncode,text[-6000:],flush=True)
            if q.returncode:raise RuntimeError('actual owner test failed')
            for row in seal['linkInputs']:sdk.verify(row)
        r['success']=True
    finally:
        r['inputsBefore']=list(tracked.values());r['changedInputs']=[row['path']for row in tracked.values()if sdk.identity(row['path'])!=row];r['inputsUnchanged']=not r['changedInputs'];r['success']&=r['inputsUnchanged'];save()
    if not r['success']:raise SystemExit(1)
if __name__=='__main__':main()
