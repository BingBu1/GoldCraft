"""Actual WinPS update transaction and real owned processes; no game is launched."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import runpy
import shutil
import subprocess
import tempfile
import time

ROOT=Path(__file__).resolve().parents[2]
PS=Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(output):
    output=output.absolute()
    if output.exists() or not output.is_relative_to(ROOT/'build') or any(p.is_symlink() or p.is_junction() for p in (output,*output.parents)):
        raise ValueError('Use a fresh plain build fixture directory')
    output.mkdir(parents=True)
    report=dict(success=False,checks=[],calls=[],realGameLaunched=False,remoteModified=False)
    targets=[]
    def check(condition,label):
        if not condition:raise AssertionError(label)
        report['checks'].append(label);print('PASS '+label,flush=True)
    def ps(script,*args):
        # A detached child can inherit a Python capture pipe even after the
        # PowerShell launcher exits. Ordinary files keep the test wait bounded.
        with tempfile.TemporaryFile(dir=output) as log:
            result=subprocess.run([str(PS),'-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',str(script),*map(str,args)],
                                  stdout=log,stderr=log,timeout=50,creationflags=subprocess.CREATE_NO_WINDOW)
            log.seek(0);result.stdout=log.read();result.stderr=b'';return result
    def put(root,name,data):
        p=root/name;p.parent.mkdir(parents=True,exist_ok=True)
        p.write_bytes(data.encode('utf-8') if isinstance(data,str) else data)
    def identities(root):
        return {role:json.loads((root/'.state'/f'{role}.json').read_text('utf-8-sig')) for role in ('cs','minecraft')}
    def alive(record):
        # PID plus creation ticks is checked by the production helper; the fixture
        # asserts the same process has not exited by querying its OS handle.
        import ctypes
        k=ctypes.WinDLL('kernel32',use_last_error=True)
        k.OpenProcess.restype=ctypes.c_void_p;k.OpenProcess.argtypes=[ctypes.c_uint,ctypes.c_int,ctypes.c_uint]
        k.CloseHandle.argtypes=[ctypes.c_void_p];k.GetExitCodeProcess.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_uint)]
        h=k.OpenProcess(0x1000,False,record['pid'])
        if not h:return False
        try:
            code=ctypes.c_uint();return bool(k.GetExitCodeProcess(h,ctypes.byref(code))) and code.value==259
        finally:k.CloseHandle(h)
    def stopped(root):return all(not alive(r) for r in identities(root).values())
    def start(root):
        p=ps(root/'scripts/Start-Servers.ps1','-Hidden')
        if p.returncode:raise AssertionError(p.stdout+p.stderr)
        return identities(root)
    def call(root,package,label,*args,ok=True):
        begin=time.monotonic();p=ps(package/'Update-Server.ps1','-TargetDirectory',root,*args)
        log=output/f'call-{len(report["calls"])+1:02}.log';log.write_bytes(p.stdout+p.stderr)
        report['calls'].append(dict(label=label,exit=p.returncode,seconds=round(time.monotonic()-begin,3),log=log.name))
        if (p.returncode==0)!=ok:raise AssertionError(label+'\n'+log.read_text(errors='replace')[-5000:])
        check(True,label)
    compiler=Path(os.environ['SystemRoot'])/'Microsoft.NET/Framework/v4.0.30319/csc.exe'
    worker=output/'worker.exe'
    build=subprocess.run([str(compiler),'/nologo','/optimize+','/out:'+str(worker),str(Path(__file__).with_name('worker.cs'))],capture_output=True)
    (output/'compile.log').write_bytes(build.stdout+build.stderr)
    if build.returncode:raise AssertionError('fixture compiler failed')
    library=runpy.run_path(str(ROOT/'tools/server-update-lifecycle/package.py'))
    original=(ROOT/'tools/current-server-update/Update-Server.ps1').read_text('utf-8-sig')
    old_release=ROOT/'server-packages/download-recovery-20261116/GoldCraft-Server-Update-20261116-Nightfall-DownloadRecovery/Update-Server.ps1'
    check(library['upgrade'](library['upgrade'](old_release.read_text('utf-8-sig')))==library['upgrade'](old_release.read_text('utf-8-sig')),
          'issued installer upgrades once and remains idempotent')
    check(library['upgrade'](original)==original,'current template already contains lifecycle integration')
    old_engine=b'old reviewed fixture engine';new_engine=b'new reviewed fixture engine'
    def fixture(name):
        folder=output/name;target=folder/'server with spaces';package=folder/'patch';target.mkdir(parents=True);package.mkdir();targets.append(target)
        for rel,data in [('Start.cmd','fixture launcher'),('Stop.cmd','fixture stop'),('server-settings.json','{"owner":true}'),
                         ('cs/swds.dll',old_engine),('cs/filesystem_stdio.dll',b'filesystem'),('cs/cstrike/dlls/mp.dll',b'mp'),
                         ('cs/cstrike/server.cfg','owner settings'),('scripts/Run-Server.ps1','fixture supervisor marker')]:put(target,rel,data)
        for name in ('Common.ps1','Stop-Servers.ps1'):shutil.copy2(ROOT/'tools/server-package/scripts'/name,target/'scripts'/name)
        shutil.copy2(Path(__file__).with_name('Start-Servers.ps1'),target/'scripts/Start-Servers.ps1')
        (target/'.state').mkdir();(target/'runtime/java/bin').mkdir(parents=True)
        shutil.copy2(worker,target/'cs/hlds.exe');shutil.copy2(worker,target/'runtime/java/bin/java.exe')
        files=[];baseline=[]
        for rel,data in [('cs/swds.dll',new_engine),('cs/cstrike/models/new.txt',b'new fixture resource')]:
            put(package,'payload/'+rel,data);files.append(dict(path=rel,bytes=len(data),sha256=sha(package/'payload'/rel),priorSha256=[sha(target/rel)] if (target/rel).exists() else []))
        for rel in ['cs/filesystem_stdio.dll','cs/cstrike/dlls/mp.dll']:
            p=target/rel;baseline.append(dict(path=rel,bytes=p.stat().st_size,sha256=sha(p),priorSha256=[sha(p)],baselineRelease='fixture'))
        put(package,'update-manifest.json',json.dumps(dict(schema=1,scope='remote-deployed-delta',release='20261107-Deployed',files=files,requiredBaseline=baseline,requiredPlugins=[])))
        script=original.replace('$script:configuration=Get-ConfigurationChanges $target $manifest','$script:configuration=@{}')
        for old,new in [('5f869363a5e116ea01eaee924bd332f4521fba725e742569b7ce70c03c339549',files[0]['sha256']),
                        ('14bedc219a27811576faacddc316ae522e566c802f9c3852be1896277a7ac553',baseline[0]['sha256']),
                        ('481b52d78644e9a89ec52b343abff70b0fb292f82f2e28f6aa6d2d3b640aeefd',baseline[1]['sha256'])]:
            assert old in script;script=script.replace(old,new)
        put(package,'Update-Server.ps1',b'\xef\xbb\xbf'+script.encode())
        for name in ['Scenario.ps1','Configuration.ps1']:put(package,name,'# no configuration mutation in this fixture')
        put(package,'Publication.ps1','function Check-Publication { }')
        shutil.copy2(ROOT/'tools/scenario-update/Files.ps1',package/'Files.ps1');library['enable'](package)
        return target,package
    try:
        target,package=fixture('normal');before=start(target);config=sha(target/'server-settings.json')
        call(target,package,'plan never stops running servers','-Plan');check(identities(target)==before and all(alive(r) for r in before.values()),'read-only plan preserves running PIDs')
        p=package/'payload/cs/swds.dll';good=p.read_bytes();p.write_bytes(b'bad')
        call(target,package,'bad payload refuses before shutdown',ok=False);check(all(alive(r) for r in before.values()),'preflight failure leaves both roles running');p.write_bytes(good)
        call(target,package,'running dual server automatically saves stops installs and restarts')
        after=identities(target);check(all(alive(r) for r in after.values()) and all(after[k]['pid']!=before[k]['pid'] for k in before),'both original roles restarted with new process identities')
        check((target/'cs/swds.dll').read_bytes()==new_engine and sha(target/'server-settings.json')==config,'payload updated and operator configuration preserved')
        call(target,package,'already-current update avoids restart');check(identities(target)==after,'idempotent update preserves running PIDs')
        call(target,package,'manual restore automatically stops and restarts','-Restore');check((target/'cs/swds.dll').read_bytes()==old_engine and all(alive(r) for r in identities(target).values()),'restore recovers old engine and running roles')
        call(target,package,'NoRestart deliberately leaves servers stopped','-NoRestart');check(stopped(target),'NoRestart applies files without launching servers')
        target,package=fixture('stopped');call(target,package,'initially stopped server stays stopped');check(not (target/'.state/cs.json').exists(),'no unsolicited startup')
        target,package=fixture('stop-failure');before=start(target);put(target,'scripts/Stop-Servers.ps1',"throw 'fixture graceful stop failed'")
        call(target,package,'failed graceful stop refuses replacement',ok=False);check((target/'cs/swds.dll').read_bytes()==old_engine and all(alive(r) for r in before.values()),'no force kill or mutation after stop failure')
        shutil.copy2(ROOT/'tools/server-package/scripts/Stop-Servers.ps1',target/'scripts/Stop-Servers.ps1')
        target,package=fixture('failed-apply');start(target)
        helper=package/'Files.ps1';helper.write_text(helper.read_text('utf-8-sig')+'''
$script:originalAtomic=${function:Atomic}
$script:failedOnce=$false
function Atomic([string]$Target,[string]$Source,[byte[]]$Bytes){
    if(!$script:failedOnce -and $Target.EndsWith('\\cs\\swds.dll') -and $Target -notmatch 'bingsang-current-update'){
        $script:failedOnce=$true;throw 'injected engine write failure'
    }
    & $script:originalAtomic $Target $Source $Bytes
}
''','utf-8-sig')
        call(target,package,'failed apply fully rolls back and restarts original servers',ok=False)
        check((target/'cs/swds.dll').read_bytes()==old_engine and not (target/'cs/cstrike/models/new.txt').exists() and all(alive(r) for r in identities(target).values()),'failed apply returns original bytes and restores running roles')
        target,package=fixture('failed-rollback');start(target)
        helper=package/'Files.ps1';helper.write_text(helper.read_text('utf-8-sig')+'''
$script:originalAtomic=${function:Atomic}
function Atomic([string]$Target,[string]$Source,[byte[]]$Bytes){
    if($Target.EndsWith('\\cs\\swds.dll') -and $Target -notmatch 'bingsang-current-update'){throw 'injected write failure'}
    & $script:originalAtomic $Target $Source $Bytes
}
function Restore-Files([string]$Root,[string]$Backup,$Journal){throw 'injected rollback failure'}
''','utf-8-sig')
        call(target,package,'incomplete rollback leaves servers stopped',ok=False)
        check(stopped(target) and (target/'cs/cstrike/models/new.txt').exists() and
              (target/'.bingsang-current-update/20261107/files/cs/swds.dll').read_bytes()==old_engine,
              'no startup with partial payload; verified preimage retained')
        target,package=fixture('restart-failure');start(target);put(target,'.state/fail-start','fixture')
        call(target,package,'startup failure is reported separately after valid installation',ok=False)
        check(stopped(target) and (target/'cs/swds.dll').read_bytes()==new_engine,'startup error retains valid installed payload and backups')
        report['success']=True
    finally:
        for target in targets:
            # These commands address only records created by this fixture.
            for role in ['cs','minecraft']:
                file=target/'.state'/f'{role}.json'
                if file.exists():
                    r=json.loads(file.read_text('utf-8-sig'))
                    if alive(r):put(target,'.state/'+r['runId']+'.command','quit' if role=='cs' else 'stop')
        deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            records=[json.loads(p.read_text('utf-8-sig')) for t in targets for p in (t/'.state').glob('*.json')]
            if all(not alive(r) for r in records):break
            time.sleep(.1)
        report['fixturesStopped']=all(not alive(r) for r in records)
        (output/'receipt.json').write_text(json.dumps(report,indent=2)+'\n','utf-8')
    print(json.dumps(dict(success=report['success'],checks=len(report['checks']),fixturesStopped=report['fixturesStopped'])),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    run(p.parse_args().output)
