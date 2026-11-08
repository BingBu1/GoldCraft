"""Run the paired delta in WinPS 5.1 on a small isolated file tree, never a server."""
from pathlib import Path
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
import shutil
import subprocess
import time

ROOT=Path(__file__).resolve().parents[2]
PS=Path('C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe')


def sha(path):
    with Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()


def run(package,receipt_path,baseline,server_baseline,output):
    package,receipt_path,baseline,server_baseline,output=map(lambda p:p.resolve(),(package,receipt_path,baseline,server_baseline,output))
    if output.exists() or not output.is_relative_to(ROOT/'build'):
        raise ValueError('Use a fresh isolated build test directory')
    output.mkdir(parents=True);target=output/'server with spaces';target.mkdir()
    manifest=json.loads((package/'update-manifest.json').read_text())
    receipt=json.loads(receipt_path.read_text())
    report=dict(passed=False,checks=[],calls=[],runtimeModified=False,gameExecuted=False)
    state=target/'.bingsang-paired-update'/str(manifest['sequence']);journal=state/'transaction.json'
    def put(rel,data):
        p=target/rel;p.parent.mkdir(parents=True,exist_ok=True)
        temp=p.with_name(p.name+'.test.tmp');temp.write_bytes(data.encode() if isinstance(data,str) else data);os.replace(temp,p)
    def check(ok,label):
        if not ok:raise AssertionError(label)
        report['checks'].append(label);print('PASS '+label,flush=True)
    def snapshot():
        return {p.relative_to(target).as_posix():(sha(p),p.stat().st_mtime_ns) for p in target.rglob('*') if p.is_file()
                and not any(part.startswith('.bingsang-') for part in p.relative_to(target).parts)}
    def call(label,*args,ok=True):
        start=time.monotonic()
        p=subprocess.run([str(PS),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(package/'Update-Server.ps1'),
                          '-TargetDirectory',str(target),*args],capture_output=True,timeout=60,creationflags=subprocess.CREATE_NO_WINDOW)
        log=output/f'call-{len(report["calls"])+1:02}.log';log.write_bytes(p.stdout+p.stderr)
        report['calls'].append(dict(label=label,exitCode=p.returncode,seconds=round(time.monotonic()-start,3),log=log.name))
        if (p.returncode==0)!=ok:raise AssertionError(label+'\n'+log.read_text(errors='replace')[-3500:])
        check(True,label)
    fixture={'Start.cmd':'original launcher','Stop.cmd':'original stop','cs/hlds.exe':'fixture; never run',
             'server-settings.json':'{"mode":7,"port":12345,"owner":"keep"}',
             'cs/cstrike/server.cfg':'owner settings and bindings',
             'cs/cstrike/addons/amxmodx/configs/users.ini':'private fixture admins',
             'cs/cstrike/addons/amxmodx/configs/plugins.ini':'owner plugins unchanged',
             'scripts/Run-Server.ps1':'owner startup','minecraft/world/level.dat':'owner world',
             '.state/private.json':'private fixture data'}
    try:
        for rel,data in fixture.items():put(rel,data)
        for r in receipt['requiredBaseline']:put(r['path'],Path(r['source']).read_bytes())
        put('cs/swds.dll',(server_baseline/'payload/cs/swds.dll').read_bytes())
        put('cs/launcher-public/manifest.json',(baseline/'manifest.json').read_bytes())
        initial=snapshot();old_engine=(target/'cs/swds.dll').read_bytes()
        call('read-only plan','-Plan');check(snapshot()==initial and not state.exists(),'preview makes no mutation')
        omitted=receipt['requiredBaseline'][-1];good=(target/omitted['path']).read_bytes();(target/omitted['path']).unlink()
        call('missing reusable blob rejects before writes',ok=False);check(not state.exists(),'missing baseline creates no backup')
        put(omitted['path'],b'corrupt');call('corrupt reusable blob rejects',ok=False);put(omitted['path'],good)
        put('cs/swds.dll',b'unknown');call('unknown engine rejects',ok=False);put('cs/swds.dll',old_engine)
        old_manifest=(target/'cs/launcher-public/manifest.json').read_bytes()
        put('cs/launcher-public/manifest.json',b'unknown');call('unknown signed baseline rejects',ok=False);put('cs/launcher-public/manifest.json',old_manifest)
        kernel=ctypes.WinDLL('kernel32',use_last_error=True)
        kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
        kernel.CreateFileW.restype=wintypes.HANDLE;kernel.CloseHandle.argtypes=[wintypes.HANDLE]
        handle=kernel.CreateFileW(str(target/'cs/swds.dll'),0x80000000,1,None,3,0,None)
        if handle==ctypes.c_void_p(-1).value:raise ctypes.WinError(ctypes.get_last_error())
        try:call('actual Windows DLL lock rejects',ok=False)
        finally:kernel.CloseHandle(handle)
        extra=package/'payload/unlisted.txt'
        try:
            extra.write_text('fixture');call('unlisted payload rejects','-Plan',ok=False)
        finally:extra.unlink(missing_ok=True)
        update=package/'update-manifest.json';original=update.read_bytes()
        try:
            bad=json.loads(original);bad['files'][0]['path']='cs/../outside.dll';update.write_text(json.dumps(bad))
            call('traversal rejects','-Plan',ok=False)
        finally:update.write_bytes(original)
        dll=package/'payload/cs/swds.dll';raw=dll.read_bytes();check(dll.stat().st_nlink==1,'mutable payload is independently stored')
        try:
            dll.write_bytes(raw+b'corrupt');call('damaged engine payload rejects','-Plan',ok=False)
        finally:dll.write_bytes(raw)
        # Inject one write failure after new blobs have been installed but before
        # the engine/publication commit. The real updater must restore everything.
        helper=package/'Files.ps1';original_helper=helper.read_bytes()
        try:
            helper.write_bytes(original_helper+b'''\r\n$script:atomicImpl=${function:Atomic}
$script:failedOnce=$false
function Atomic([string]$Target,[string]$Source,[byte[]]$Bytes) {
    if(!$script:failedOnce -and $Target.EndsWith('\\cs\\swds.dll') -and $Target -notmatch 'bingsang-paired-update'){
        $script:failedOnce=$true;throw 'injected engine replacement failure'
    }
    & $script:atomicImpl $Target $Source $Bytes
}
''')
            call('mid-transaction failure rolls back',ok=False)
            check({k:v[0] for k,v in snapshot().items()}=={k:v[0] for k,v in initial.items()},'failed apply restores original engine and publication and removes added blobs')
        finally:helper.write_bytes(original_helper)
        alias=output/'old-engine-alias.dll';os.link(target/'cs/swds.dll',alias)
        call('paired installation succeeds')
        check(all(sha(target/r['path'])==r['sha256'] for r in manifest['files']),'all 7 installed payload files match exact SHA')
        installed=snapshot();record=journal.read_bytes()
        check(sha(alias)==hashlib.sha256(old_engine).hexdigest() and alias.stat().st_ino!=(target/'cs/swds.dll').stat().st_ino,'atomic engine update preserves other hard-link owner')
        check(all(installed[k]==initial[k] for k in fixture),'every operator setting, launcher, admin and Minecraft file is byte/timestamp unchanged')
        call('second installation is idempotent');check(snapshot()==installed and journal.read_bytes()==record,'idempotent run preserves original backup')
        put('cs/cstrike/server.cfg','owner changed configuration after update')
        call('operator configuration edits remain allowed')
        call('restore paired engine and publication','-Restore')
        check((target/'cs/cstrike/server.cfg').read_text()=='owner changed configuration after update','rollback preserves later operator configuration edits')
        put('cs/cstrike/server.cfg',fixture['cs/cstrike/server.cfg'])
        check({k:v[0] for k,v in snapshot().items()}=={k:v[0] for k,v in initial.items()},'rollback restores all managed preimages')
        partial=json.loads(record);partial['status']='prepared';journal.write_text(json.dumps(partial))
        put('cs/swds.dll',raw)
        call('interrupted engine/publication pair recovers and reapplies')
        check(all(sha(target/r['path'])==r['sha256'] for r in manifest['files']),'interrupted recovery produces complete pair')
        put('cs/swds.dll',b'operator replaced engine');call('rollback refuses unknown operator DLL','-Restore',ok=False)
        check((target/'cs/swds.dll').read_bytes()==b'operator replaced engine','unknown engine remains untouched')
        put('cs/swds.dll',raw)
        backup=state/'files/cs/swds.dll';saved=backup.read_bytes();backup.write_bytes(b'corrupt')
        call('corrupt rollback backup rejects','-Restore',ok=False);backup.write_bytes(saved)
        call('final recovery remains restorable','-Restore')
        check({k:v[0] for k,v in snapshot().items()}=={k:v[0] for k,v in initial.items()},'final restored bytes equal original fixture')
        report['passed']=True
    finally:
        report['inputHashes']={n:sha(package/n) for n in ['Update-Server.ps1','Paired-Publication.ps1','Files.ps1','update-manifest.json']}
        (output/'receipt.json').write_text(json.dumps(report,indent=2)+'\n','utf-8')
    print(json.dumps(dict(passed=True,checks=len(report['checks']),calls=len(report['calls']))),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('package','receipt','baseline','server-baseline','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();run(a.package,a.receipt,a.baseline,a.server_baseline,a.output)
