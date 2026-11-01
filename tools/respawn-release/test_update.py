"""Exercise the shipped WinPS installer against the complete 1429-file baseline."""
from pathlib import Path
import argparse
import base64
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
import shutil
import subprocess
import time
import zipfile

ROOT=Path(__file__).resolve().parents[2]
STATE='.bingsang-respawn-release'


def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()


def main(package, output, reuse=None):
    if output.exists() or not output.is_relative_to(ROOT/'build/respawn-unified-20261101'):
        raise ValueError('Use a fresh owned test output')
    output.mkdir(parents=True)
    target=reuse or output/'server with spaces'
    if reuse:
        if not target.is_relative_to(ROOT/'build/respawn-unified-20261101') or target.is_symlink() or target.is_junction():
            raise ValueError('Reusable baseline must be an owned ordinary test directory')
    else:target.mkdir()
    manifest=json.loads((package/'update-manifest.json').read_text('utf-8-sig'))
    assert len(manifest['requiredBaseline'])==1429 and len(manifest['files'])==80
    report=dict(passed=False,checks=[],calls=[],completeBaselineFiles=1429,completePayloadFiles=80,
                fullProductionManifestApplied=True,remoteDeployed=False)
    state=target/STATE;journal=state/'transaction.json'
    if state.exists():
        if not reuse or json.loads(journal.read_text('utf-8-sig'))['status']!='restored':
            raise ValueError('Previous fixture must be completely restored before reuse')
        state.rename(output/'prior-restored-state')
    prior=ROOT/'build/remote-scenario-package-20261101/launcher-public'
    audit=json.loads((ROOT/'build/csol-respawn-remote-closure-20261101/plan-03/baseline-audit.json').read_text('utf-8-sig'))
    old={x['path'].lower():x for x in audit['effectiveFiles']}
    archives={}
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
    kernel.CreateFileW.restype=wintypes.HANDLE
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]

    def check(ok,label):
        if not ok:raise AssertionError(label)
        report['checks'].append(label);print('PASS '+label,flush=True)

    def put(name,data):
        p=target/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)

    def baseline(name):
        row=old[name.lower()];data=archives[row['archive']].read(row['member'])
        assert len(data)==row['bytes'] and hashlib.sha256(data).hexdigest()==row['sha256']
        return data

    def snapshot():
        return {p.relative_to(target).as_posix().lower():sha(p) for p in target.rglob('*') if p.is_file() and STATE not in p.parts}

    def state_snapshot():
        return {p.relative_to(state).as_posix():(sha(p),p.stat().st_mtime_ns) for p in state.rglob('*') if p.is_file()}

    def call(*args,success=True,cmd=False):
        env=dict(os.environ);env.pop('PSModulePath',None)
        if cmd:
            command=f'cmd.exe /d /s /c ""{package / "Update-Server.cmd"}" -TargetDirectory "{target}" '+ ' '.join(args)+'"'
        else:
            command=['powershell.exe','-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',str(package/'Update-Server.ps1'),'-TargetDirectory',str(target),*args]
        start=time.monotonic();p=subprocess.run(command,input=b'\r\n',capture_output=True,timeout=120,env=env,creationflags=subprocess.CREATE_NO_WINDOW)
        log=output/f'call-{len(report["calls"])+1:02}.log';log.write_bytes(p.stdout+p.stderr)
        report['calls'].append(dict(args=args,cmd=cmd,exitCode=p.returncode,seconds=round(time.monotonic()-start,3),log=log.name))
        if (p.returncode==0)!=success:raise AssertionError(log.read_bytes().decode(errors='replace')[-4000:])
        return p

    try:
        version=subprocess.check_output(['powershell.exe','-NoProfile','-Command','$PSVersionTable.PSVersion.ToString()'])
        check(version.startswith(b'5.1.'),'Actual Windows PowerShell 5.1')
        for row in audit['archives']:
            p=Path(row['identity']['path']);assert sha(p)==row['identity']['sha256'];archives[str(p)]=zipfile.ZipFile(p)
        for row in manifest['requiredBaseline']:
            data=baseline(row['path']);assert hashlib.sha256(data).hexdigest()==row['sha256'];put(row['path'],data)
        old_mp=baseline('cs/cstrike/dlls/mp.dll');engine=baseline('cs/swds.dll')
        put('cs/cstrike/dlls/mp.dll',old_mp);put('cs/swds.dll',engine)
        for name,text in {'Start.cmd':'original launcher','Stop.cmd':'original stopper','cs/hlds.exe':'shape-only never executed',
                          'server-settings.json':'{"map":"zs_deadend","csolGameMode":2}',
                          'cs/cstrike/server.cfg':'hostname owner\n','cs/cstrike/addons/amxmodx/configs/users.ini':'fixture admin',
                          'minecraft/world/level.dat':'fixture save'}.items():put(name,text.encode())
        old_manifest=(prior/'manifest.json').read_bytes();put('cs/launcher-public/manifest.json',old_manifest)
        release=json.loads(base64.b64decode(json.loads(old_manifest)['payload']))
        for row in release['files']:
            blob='blobs/'+row['sha256']+'/'+Path(row['path']).name
            assert sha(prior/blob)==row['sha256'];dest=target/'cs/launcher-public'/blob;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(prior/blob,dest)
        initial=snapshot();mp=target/'cs/cstrike/dlls/mp.dll';signed=target/'cs/launcher-public/manifest.json'
        check(len(manifest['requiredBaseline'])==1429,'All 1429 actual baseline resources materialized from verified historical ZIPs')
        call('-Plan',cmd=True);check(snapshot()==initial and not state.exists(),'Actual CMD preview preserves full runtime and creates no state')
        signed.write_bytes(b'unknown signed publication');before=snapshot();call(success=False)
        check(snapshot()==before and not state.exists(),'Unknown existing release rejects without writes');signed.write_bytes(old_manifest)
        first=target/manifest['requiredBaseline'][0]['path'];data=first.read_bytes();first.write_bytes(b'bad baseline');before=snapshot();call(success=False)
        check(snapshot()==before and not state.exists(),'Changed omitted resource rejects without writes');first.write_bytes(data)
        hold=kernel.CreateFileW(str(signed),0x80000000,1,None,3,0,None)
        if hold==ctypes.c_void_p(-1).value:raise ctypes.WinError(ctypes.get_last_error())
        try:call(success=False)
        finally:kernel.CloseHandle(hold)
        check(snapshot()==initial and json.loads(journal.read_text('utf-8-sig'))['status']=='restored',
              'Locked final manifest rolls back all earlier resource, client blob and server DLL writes')
        call();installed=snapshot();expected=dict(initial)
        expected.update({x['path'].lower():x['sha256'] for x in manifest['files']})
        check(installed==expected,'Only the exact 80 paired payloads change; settings, FastDL, saves and old core blobs preserved')
        check(mp.stat().st_nlink==1 and (state/'files/cs/cstrike/dlls/mp.dll').stat().st_nlink==1,
              'Installed and rollback DLLs are independent files')
        stable=state_snapshot();call();call('-Plan')
        check(snapshot()==installed and state_snapshot()==stable,'Repeat apply and preview preserve runtime and rollback timestamps')
        call('-Restore');check(snapshot()==initial,'Rollback restores exact full baseline and previous signed release')
        stable=state_snapshot();call('-Restore');check(snapshot()==initial and state_snapshot()==stable,'Repeated rollback is idempotent')
        call();saved=state/'files/cs/launcher-public/manifest.json';data=saved.read_bytes();saved.write_bytes(b'bad backup');call('-Restore',success=False)
        check(snapshot()==installed,'Corrupt publication backup rejects rollback without writes');saved.write_bytes(data)
        j=json.loads(journal.read_text('utf-8-sig'));j['status']='prepared';journal.write_text(json.dumps(j),'utf-8')
        mp.write_bytes(old_mp);call();check(snapshot()==installed,'Interrupted mixed transaction recovers both sides then reapplies')
        call('-Restore')
        # Upgrade users who already applied the previous server-only free revival package.
        for row in manifest['files']:
            if row['path'].startswith('cs/cstrike/'):put(row['path'],(package/'payload'/row['path']).read_bytes())
        server_only=snapshot();call();j=json.loads(journal.read_text('utf-8-sig'))
        check(len(j['files'])==2 and snapshot()==installed,'Previously installed server-only revival needs only the client blob and signature')
        call('-Restore');check(snapshot()==server_only,'Rollback of server-only upgrade preserves preexisting revival DLL and assets')
        report['passed']=True
        report['packageManifestSha256']=sha(package/'update-manifest.json')
        report['fixtureLogicalBytes']=sum(p.stat().st_size for p in target.rglob('*') if p.is_file())
    finally:
        for z in archives.values():z.close()
        (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n','utf-8')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--package',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--reuse-baseline',type=Path)
    a=p.parse_args();main(a.package.resolve(),a.output.resolve(),a.reuse_baseline.resolve() if a.reuse_baseline else None)
