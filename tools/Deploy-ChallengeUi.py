#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply a reviewed local single-B challenge UI plan during an authorized restart."""
import argparse
import ctypes
from datetime import datetime,timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import time
from win32_processes import processes
import csol_hud_resources

ROOT=Path(__file__).resolve().parent.parent
CLIENT=ROOT/'sandbox/cs-client-b'
SERVER=ROOT/'sandbox/cs-server'


def load(path):return json.loads(path.read_text('utf-8-sig'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();plan=load(args.plan);out=args.output.resolve()
    if not out.is_relative_to(ROOT/'build')or(out/'deployment.json').exists():raise ValueError('Use unused workspace receipt directory')
    out.mkdir(parents=True,exist_ok=True);(out/'rollback').mkdir(exist_ok=True)
    guard=module('challenge_atomic',ROOT/'tools/Build-CSOLCandidate.py')
    remote=module('challenge_rcon',ROOT/'tools/GoldSrc-Command.py')
    for row in plan['files']:
        source,target=Path(row['source']),Path(row['target'])
        if not source.resolve(strict=True).is_relative_to(ROOT)or not any(target.resolve().is_relative_to(root.resolve())for root in(CLIENT,SERVER)):
            raise ValueError('Plan is outside the explicitly selected sandbox runtimes')
        if sha(source)!=row['sourceSha256']or(sha(target)if target.is_file()else None)!=row['beforeSha256']:
            raise ValueError('Plan input changed: '+str(target))
    record=load(CLIENT/'process-CsClient.json')
    current=[row for row in processes(('MetaHook.exe',))if row['pid']==record['pid']and Path(row['image']).resolve()==(CLIENT/'Half-Life/MetaHook.exe').resolve()]
    if len(current)!=1:raise ValueError('Recorded single B is not the current process')
    started=datetime.fromisoformat(record['startedUtc'].replace('Z','+00:00')).timestamp()
    if abs(current[0]['creationTime100ns']/1e7-11644473600-started)>=1:raise ValueError('B creation identity changed')
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.OpenProcess.argtypes=[ctypes.c_uint32,ctypes.c_int,ctypes.c_uint32];kernel.OpenProcess.restype=ctypes.c_void_p
    kernel.TerminateProcess.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
    kernel.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_uint32];kernel.CloseHandle.argtypes=[ctypes.c_void_p]
    handle=kernel.OpenProcess(0x100001,False,record['pid'])
    if not handle:raise ctypes.WinError(ctypes.get_last_error())
    try:
        if not kernel.TerminateProcess(handle,0)or kernel.WaitForSingleObject(handle,10000)!=0:raise RuntimeError('Single B did not stop')
    finally:kernel.CloseHandle(handle)
    try:remote.command('quit',config_path=SERVER/'challenge-ui-rcon-private.json')
    except TimeoutError:pass
    for _ in range(30):
        if not any(row.get('image')and Path(row['image']).resolve()==(SERVER/'Half-Life/hlds.exe').resolve()for row in processes(('hlds.exe',))):break
        time.sleep(.1)
    else:raise RuntimeError('Server still running; no files replaced')
    changed,temporary=[],[]
    result=dict(success=False,sourcePublished=plan['sourcePublished'],files=changed,temporary=temporary,temporaryRestored=False,started=False)

    def apply(source,target,bucket):
        before=sha(target)if target.is_file()else None;after=sha(source)
        if before==after:return
        backup=out/'rollback'/str(len(changed)+len(temporary))
        if before:
            shutil.copy2(target,backup)
            if sha(backup)!=before:raise ValueError('Backup hash mismatch')
        row=dict(target=str(target),beforeSha256=before,afterSha256=after,backup=str(backup)if before else None)
        bucket.append(row);guard.atomic_copy(source,target)
        if sha(target)!=after:raise ValueError('Deployment hash mismatch')

    try:
        for row in plan['files']:apply(Path(row['source']),Path(row['target']),changed)
        game=SERVER/'Half-Life/cstrike';config=game/'addons/goldcraft/config/gameplay.json';settings=load(config)
        settings['features']['time_cycle']['enabled']=True
        settings['features']['time_cycle']['values']['midday_seconds']['value']=8
        settings['features']['time_cycle']['values']['dusk_seconds']['value']=12
        settings['features']['cof_sprint']['values']['kick_cost']['value']=100
        (out/'test-gameplay.json').write_text(json.dumps(settings,ensure_ascii=False,indent=2)+'\n','utf8')
        apply(out/'test-gameplay.json',config,temporary)
        plugins=game/'addons/amxmodx/configs/plugins.ini';text=plugins.read_text('utf-8-sig')
        if 'goldcraft_phase_audio_live.amxx'in text:raise ValueError('Unknown existing observer ownership')
        (out/'test-plugins.ini').write_text(text+'\ngoldcraft_phase_audio_live.amxx debug\n','utf8')
        apply(out/'test-plugins.ini',plugins,temporary)
        apply(out/'goldcraft_phase_audio_live.amxx',game/'addons/amxmodx/plugins/goldcraft_phase_audio_live.amxx',temporary)
        for root,name in((CLIENT,'csol-support-deployment.json'),(SERVER,'rehlds-deployment.json')):
            path=root/name;value=load(path)
            target=root/'Half-Life'/('cstrike/metahook/plugins/CSOLSupport.dll'if root==CLIENT else'cstrike/dlls/mp.dll')
            value['challengeUi']=dict(commit=plan['sourcePublished'],sha256=sha(target),evidence=str(out/'deployment.json'))
            if root==CLIENT:value['sha256']=sha(target);value['deployedUtc']=datetime.now(timezone.utc).isoformat()
            else:
                for row in value['files']:
                    if row.get('name')=='cstrike/dlls/mp.dll':row['sha256']=sha(target).upper()
            prepared=out/(name+'.new');prepared.write_text(json.dumps(value,indent=2)+'\n','utf8');apply(prepared,path,changed)
        result['hudPreflight']=csol_hud_resources.verify(game)
        if not result['hudPreflight']['success']:raise ValueError('Complete HUD preflight failed')
        result['success']=True
    except BaseException as error:
        result['error']=type(error).__name__+': '+str(error)
        for row in reversed(changed+temporary):
            target=Path(row['target'])
            if row['backup']:guard.atomic_copy(Path(row['backup']),target)
            elif target.exists():target.unlink()
        result['rolledBack']=True
        raise
    finally:(out/'deployment.json').write_text(json.dumps(result,indent=2)+'\n','utf8')
    print(json.dumps(dict(success=True,changed=len(changed),temporary=len(temporary),started=False,hudResources=result['hudPreflight']['resources'])))


if __name__=='__main__':main()
